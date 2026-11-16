{
    const QMap<Shape, QStringList> expected = {
        {Shape::Box, {"width", "depth", "height"}},
        {Shape::Sphere, {"radius", "segments"}},
        {Shape::Cylinder, {"radius", "height", "segments"}},
        {Shape::Cone, {"radius", "height", "segments"}},
        {Shape::Torus, {"radius", "tubeRadius", "segments"}},
        {Shape::Tube, {"radius", "tubeRadius", "height", "segments"}},
        {Shape::Frustum, {"radius", "tubeRadius", "height", "segments"}},
        {Shape::Ellipsoid, {"width", "depth", "height", "segments"}},
        {Shape::Wedge, {"width", "depth", "height"}}};
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const auto shape = it.key();
        const auto keys = it.value();
        const auto before = serialize(document_.scene());
        const int historyBefore = document_.history()->index();
        bool matched = false;
        QTimer::singleShot(0, this, [&] {
            auto *d = findChild<QDialog *>("createBodyDialog");
            if (!d)
                return;
            matched = serialize(document_.scene()) == before;
            for (const auto &key :
                 QStringList{"width", "depth", "height", "radius", "tubeRadius", "segments"})
                matched = matched && (bool(d->findChild<QWidget *>("create_" + key)) == keys.contains(key));
            if (shape == Shape::Torus)
                d->grab().save(directory + "/create-torus.png");
            d->reject();
        });
        findChild<QAction *>("create" + shapeKey(shape))->trigger();
        check(matched, "creation dialog exposes only relevant fields: " + shapeKey(shape));
        check(serialize(document_.scene()) == before && document_.history()->index() == historyBefore,
              "cancel creation does not alter scene: " + shapeKey(shape));
    }
    const int countBefore = document_.scene().bodies.size(), historyBefore = document_.history()->index();
    bool rejected = false;
    QTimer::singleShot(0, this, [&] {
        auto *d = findChild<QDialog *>("createBodyDialog");
        if (!d)
            return;
        d->findChild<QDoubleSpinBox *>("create_radius")->setValue(10);
        d->findChild<QDoubleSpinBox *>("create_tubeRadius")->setValue(20);
        d->findChild<QPushButton *>("createBodyConfirm")->click();
        rejected = document_.scene().bodies.size() == countBefore &&
                   document_.history()->index() == historyBefore &&
                   !d->findChild<QLabel *>("createFeedback")->text().isEmpty();
        if (!rejected) {
            d->reject();
            return;
        }
        d->findChild<QDoubleSpinBox *>("create_tubeRadius")->setValue(3);
        d->findChild<QDoubleSpinBox *>("create_height")->setValue(40);
        d->findChild<QSpinBox *>("create_segments")->setValue(24);
        d->findChild<QLineEdit *>("createName")->setText(QStringLiteral("自定义圆管"));
        d->findChild<QPushButton *>("createBodyConfirm")->click();
    });
    findChild<QAction *>("createtube")->trigger();
    check(rejected, "invalid dimensions keep dialog open without partial creation");
    const auto *created = document_.scene().find(selection_);
    check(created && created->name == QStringLiteral("自定义圆管") && created->params.radius == 10 &&
              created->params.tubeRadius == 3 && created->params.height == 40 &&
              created->params.segments == 24,
          "confirmed creation uses supplied dimensions name and subdivision");
    check(numbers_["width"]->isHidden() && !numbers_["radius"]->isHidden() && !segments_->isHidden(),
          "inspector hides unrelated dimensions for tube");
    check(document_.scene().bodies.size() == countBefore + 1 &&
              document_.history()->index() == historyBefore + 1,
          "parameterized creation is one undo command");
    document_.history()->undo();
    check(document_.scene().bodies.size() == countBefore, "undo removes the parameterized model");
}
