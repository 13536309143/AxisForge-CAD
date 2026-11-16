{
    Scene single;
    auto groundBody = makeBody(Shape::Box);
    groundBody.position = {10, 20, -80};
    single.bodies << groundBody;
    document_.replace(single);
    selectBody(groundBody.id);
    findChild<QAction *>("alignFloor")->trigger();
    check(document_.scene().mates.size() == 1 && document_.scene().mates[0].type == MateType::Ground,
          "floor button creates persistent ground mate");
    check(std::abs(minimumWorldZ(document_.scene().bodies[0])) < .001,
          "ground mate lifts model above world floor");
    document_.edit("Resize grounded model", [](Scene &s) { s.bodies[0].params.height = 80; });
    check(std::abs(minimumWorldZ(document_.scene().bodies[0])) < .001,
          "ground mate maintains contact after resize");
    const QString groundId = document_.scene().mates[0].id;
    openMateEditor(groundId);
    check(mateEditor_ && mateEditor_->findChild<QComboBox *>("mateType")->count() == 8,
          "ground mate editor works with a single model");
    check(mateEditor_->findChild<QComboBox *>("mateSource")->currentData().toString() == "@ground",
          "fixed floor is the mate source");
    mateEditor_->findChild<QDoubleSpinBox *>("mateOffset")->setValue(10);
    mateEditor_->findChild<QPushButton *>("mateApply")->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    check(std::abs(minimumWorldZ(document_.scene().bodies[0]) - 10) < .001, "ground clearance editing works");
    viewport_->fitAll();
    check(viewport_->grabFramebuffer().save(directory + "/ground-mate.png"),
          "ground mate annotation screenshot");
    document_.history()->undo();
    check(std::abs(minimumWorldZ(document_.scene().bodies[0])) < .001,
          "undo ground clearance restores contact");
}
