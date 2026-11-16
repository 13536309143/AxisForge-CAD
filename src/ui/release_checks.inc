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
    viewport_->grabFramebuffer();
    check(viewport_->gridPlan().z==0,"floor fixed before alignment");
    findChild<QAction *>("alignFloor")->trigger();
    check(std::abs(visibleWorldBounds(document_.scene()).min.z()) < .001, "rotated box aligns to floor");
    viewport_->grabFramebuffer();
    check(viewport_->gridPlan().z==0,"alignment moves model only");
    document_.history()->undo();
    viewport_->grabFramebuffer();
    check(viewport_->gridPlan().z==0,"undo never changes floor height");
    check(vectorsClose(document_.scene().bodies[0].position, floorBody.position), "floor alignment undo");
    check(findChild<QToolBar *>("fileToolbar") && findChild<QToolBar *>("placementToolbar") &&
              findChild<QToolBar *>("viewToolbar"),
          "organized toolbar groups");
}

{
    Scene below;auto body=makeBody(Shape::Sphere);body.position={0,0,-80};below.bodies<<body;document_.replace(below);selectBody(body.id);
    findChild<QAction *>("alignFloor")->trigger();viewport_->grabFramebuffer();
    check(std::abs(document_.scene().bodies[0].position.z()-15)<.001&&viewport_->gridPlan().z==0,"below-ground sphere moves above fixed floor");
}
