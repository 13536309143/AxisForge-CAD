{
    for (auto shape : {Shape::Tube, Shape::Frustum, Shape::Ellipsoid, Shape::Wedge}) {
        auto *createAction = findChild<QAction *>("create" + shapeKey(shape));
        check(createAction, "new shape action");
        confirmNextCreation();createAction->trigger();
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
{
    Scene s; auto sphere=makeBody(Shape::Sphere);sphere.params.segments=96;
    s.bodies<<sphere;document_.replace(s);viewport_->setSelection({});
    viewport_->setDisplayMode(Viewport::DisplayMode::Normal);viewport_->fitAll();
    viewport_->grabFramebuffer();
    const auto full=viewport_->metrics();
    check(full.drawnInstances==1 && full.submittedTriangles>0,"LOD initial frame");
    QWheelEvent zoom(QPointF(200,200),QPointF(200,200),QPoint(),QPoint(0,-3600),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(viewport_,&zoom);viewport_->grabFramebuffer();
    const auto low=viewport_->metrics();
    check(low.lodInstances[2]==1 && low.submittedTriangles<full.submittedTriangles,"distant solid uses fewer triangles");
    viewport_->setSelection(sphere.id);viewport_->grabFramebuffer();
    check(viewport_->metrics().submittedTriangles==full.submittedTriangles,"selected solid retains full resolution");
    viewport_->setSelection({});viewport_->setLodEnabled(false);viewport_->grabFramebuffer();
    check(viewport_->metrics().submittedTriangles==full.submittedTriangles,"LOD disable restores full triangles");
    viewport_->setLodEnabled(true);
    auto offscreen=makeBody(Shape::Box);offscreen.position={100000,0,0};s.bodies<<offscreen;
    viewport_->setScene(s);viewport_->grabFramebuffer();
    check(viewport_->metrics().culledInstances==1 && viewport_->metrics().drawnInstances==1,"offscreen instance culled");
    viewport_->setFrustumCulling(false);viewport_->grabFramebuffer();
    check(viewport_->metrics().drawnInstances==2 && viewport_->metrics().culledInstances==0,"culling toggle submits all instances");
    viewport_->setFrustumCulling(true);viewport_->setScene(document_.scene());viewport_->fitAll();
    check(viewport_->grabFramebuffer().save(directory+"/lod-culling.png"),"LOD screenshot");
}
