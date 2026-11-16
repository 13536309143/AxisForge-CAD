{
    for (auto shape : {Shape::Tube, Shape::Frustum, Shape::Ellipsoid, Shape::Wedge}) {
        auto *createAction = findChild<QAction *>("create" + shapeKey(shape));
        check(createAction, "new shape action");
        createAction->trigger();
        check(document_.scene().find(selection_)->shape == shape, "new shape UI creation");
        document_.history()->undo();
    }
    Scene floorScene;
    auto floorBody = makeBody(Shape::Box);
    floorBody.position = {10, 20, 80};
    floorBody.rotation = QQuaternion::fromEulerAngles(17, 35, 12);
    floorScene.bodies << floorBody;
    document_.replace(floorScene);
    selectBody(floorBody.id);
    findChild<QAction *>("alignFloor")->trigger();
    check(std::abs(visibleWorldBounds(document_.scene()).min.z()) < .001, "rotated box aligns to floor");
    document_.history()->undo();
    check(vectorsClose(document_.scene().bodies[0].position, floorBody.position), "floor alignment undo");
    check(findChild<QToolBar *>("fileToolbar") && findChild<QToolBar *>("placementToolbar") &&
              findChild<QToolBar *>("viewToolbar"),
          "organized toolbar groups");
}
