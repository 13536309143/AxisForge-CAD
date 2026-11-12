{
    Scene pair;
    auto a = makeBody(Shape::Box), b = makeBody(Shape::Cylinder);
    a.name = QStringLiteral("基准 A");
    b.name = QStringLiteral("从动 B");
    a.fixed = true;
    a.position = {-45, -15, 20};
    b.position = {45, 15, 55};
    pair.bodies = {a, b};
    auto flushDialogs = [] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
    };
    for (int i = 0; i < 7; ++i) {
        Scene s = pair;
        Mate m;
        m.id = "visual-" + QString::number(i);
        m.source = a.id;
        m.target = b.id;
        m.type = MateType(i);
        m.angle = 45;
        m.offset = 30;
        m.distance = 95;
        s.mates << m;
        check(solve(s, &why), "guide fixture solves " + QString::number(i));
        document_.replace(s);
        viewport_->fitAll();
        viewport_->grabFramebuffer();
        const auto guideBufferBytes = viewport_->metrics().gpuBufferBytes;
        mateTree_->setCurrentItem(mateTree_->topLevelItem(0));
        check(viewport_->selectedMate() == m.id, "tree selects mate in viewport " + QString::number(i));
        const auto guide = viewport_->currentMateGuide();
        check(guide.valid && guide.visible && guide.satisfied && !guide.lines.isEmpty() &&
                  !guide.labels.isEmpty(),
              "selected mate has geometric guides " + QString::number(i));
        check(viewport_->grabFramebuffer().save(directory + QString("/mate-%1.png").arg(i)),
              "mate screenshot " + QString::number(i));
        check(viewport_->metrics().gpuBufferBytes == guideBufferBytes && document_.history()->index() == 0,
              "mate selection reuses GPU buffers and does not edit " + QString::number(i));
    }
    selectBody(a.id);
    check(viewport_->selectedMate().isEmpty(), "pickedBody selection clears mate highlight");
    selectMate(document_.scene().mates[0].id);
    deleteMate();
    check(document_.scene().mates.isEmpty() && viewport_->selectedMate().isEmpty(),
          "deleted mate guide disappears");
    document_.history()->undo();
    check(document_.scene().mates.size() == 1, "undo restores removed extended mate");

    document_.replace(pair);
    viewport_->fitAll();
    const auto beforePreview = serialize(document_.scene());
    addMateDialog();
    QPointer<QDialog> editor = mateEditor_;
    check(editor && !editor->isModal(), "mate editor allows scene interaction");
    auto *preview = editor->findChild<QCheckBox *>("matePreview");
    preview->setChecked(false);
    check(editor->findChild<QComboBox *>("mateType")->count() == 7, "seven mate types available");
    editor->findChild<QPushButton *>("matePickPair")->click();
    for (const auto &pickedBody : pair.bodies) {
        auto pickedPoint = viewport_->projectPoint(pickedBody.position);
        QMouseEvent matePress(QEvent::MouseButtonPress, QPointF(pickedPoint), Qt::LeftButton, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(viewport_, &matePress);
        QMouseEvent mateRelease(QEvent::MouseButtonRelease, QPointF(pickedPoint), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport_, &mateRelease);
    }
    check(editor->findChild<QComboBox *>("mateSource")->currentData().toString() == a.id &&
              editor->findChild<QComboBox *>("mateTarget")->currentData().toString() == b.id,
          "scene picking fills A then B");
    check(serialize(document_.scene()) == beforePreview, "scene pair picking does not edit document");
    editor->findChild<QComboBox *>("mateType")->setCurrentIndex(int(MateType::Angle));
    editor->findChild<QDoubleSpinBox *>("mateAngle")->setValue(37);
    preview->setChecked(true);
    check(viewport_->currentMateGuide().satisfied && serialize(document_.scene()) == beforePreview,
          "live preview solves without mutating document");
    check(editor->grab().save(directory + "/mate-editor.png"), "modeless mate editor screenshot");
    check(viewport_->grabFramebuffer().save(directory + "/mate-preview.png"),
          "uncommitted mate preview screenshot");
    editor->reject();
    flushDialogs();
    check(serialize(document_.scene()) == beforePreview && document_.history()->index() == 0 &&
              viewport_->selectedMate().isEmpty(),
          "cancel preview restores scene without history");

    addMateDialog();
    editor = mateEditor_;
    editor->findChild<QComboBox *>("mateType")->setCurrentIndex(int(MateType::Angle));
    editor->findChild<QDoubleSpinBox *>("mateAngle")->setValue(37);
    editor->findChild<QPushButton *>("mateApply")->click();
    flushDialogs();
    check(document_.scene().mates.size() == 1 && document_.history()->index() == 1,
          "applying mate creates one undo transaction");
    const QString createdId = document_.scene().mates[0].id;
    check(viewport_->selectedMate() == createdId &&
              residual(document_.scene(), document_.scene().mates[0]).satisfied(),
          "applied mate is selected and satisfied");
    openMateEditor(createdId);
    editor = mateEditor_;
    check(editor->findChild<QDoubleSpinBox *>("mateAngle")->value() == 37,
          "editing loads existing parameters");
    editor->findChild<QDoubleSpinBox *>("mateAngle")->setValue(62);
    editor->findChild<QPushButton *>("mateApply")->click();
    flushDialogs();
    check(document_.scene().mates.size() == 1 && document_.scene().mates[0].id == createdId &&
              document_.scene().mates[0].angle == 62,
          "editing preserves mate identity");
    document_.history()->undo();
    check(document_.scene().mates[0].angle == 37 && viewport_->selectedMate() == createdId,
          "undo edit updates selected guide");
    check(saveFile(directory + "/extended-mates.afcad") && openFile(directory + "/extended-mates.afcad"),
          "extended mates project roundtrip");
    check(document_.scene().mates[0].angle == 37, "extended angle persists on disk");
    selectMate(createdId);
    document_.edit("Disable visual mate", [&](Scene &s) { s.mates[0].enabled = false; });
    check(!viewport_->currentMateGuide().enabled, "disabled mate is marked disabled");

    document_.replace(pair);
    openMateEditor({});
    editor = mateEditor_;
    document_.removeBody(b.id);
    check(!editor->findChild<QPushButton *>("mateApply")->isEnabled(),
          "editor handles deleted referenced object safely");
    editor->reject();
    flushDialogs();
    check(document_.scene().bodies.size() == 1, "cancel preserves external changes during preview");

    Scene large;
    auto largeBox = makeBody(Shape::Box);
    largeBox.params.width = 6000;
    largeBox.params.depth = 4500;
    largeBox.params.height = 300;
    largeBox.position = {2000, -1700, 150};
    large.bodies << largeBox;
    document_.replace(large);
    viewport_->fitAll();
    viewport_->grabFramebuffer();
    const auto plan = viewport_->gridPlan();
    const auto bounds = visibleWorldBounds(large);
    check(plan.step > 10 && plan.minX <= bounds.min.x() && plan.maxX >= bounds.max.x() &&
              plan.minY <= bounds.min.y() && plan.maxY >= bounds.max.y(),
          "floor expands around large positioned model");
    check(viewport_->grabFramebuffer().save(directory + "/adaptive-grid.png"), "adaptive floor screenshot");
    document_.edit("Hide large model", [](Scene &s) { s.bodies[0].visible = false; });
    viewport_->fitAll();
    viewport_->grabFramebuffer();
    check(viewport_->gridPlan().step < plan.step, "floor readapts after hiding large model and fitting view");
}
