test("hierarchy nested rigid edits and parent rotation", [] {
    Scene s;auto root=makeBody(Shape::Group),child=makeBody(Shape::Box),grand=makeBody(Shape::Sphere);
    child.parentId=root.id;child.position={10,0,0};child.fixed=true;
    grand.parentId=child.id;grand.position={11,0,0};s.bodies={grand,child,root};
    CHECK(s.validate());Scene before=s;
    s.find(root.id)->position={5,0,0};s.find(root.id)->rotation=QQuaternion::fromEulerAngles(0,0,90);
    s.inheritEdits(before);
    CHECK(vectorsClose(s.find(child.id)->position,{5,10,0}));
    CHECK(vectorsClose(s.find(grand.id)->position,{5,11,0}));
    CHECK(solve(s));CHECK(vectorsClose(s.find(child.id)->position,{5,10,0}));
});
test("parent mate drives full subtree", [] {
    Scene s;auto a=makeBody(Shape::Box),g=makeBody(Shape::Group),b=makeBody(Shape::Box);
    a.position={100,20,0};a.fixed=true;b.parentId=g.id;b.position={10,0,0};s.bodies={b,g,a};
    s.mates<<mate(a,g,MateType::Coincident);
    CHECK(solve(s));CHECK(vectorsClose(s.find(g.id)->position,a.position));
    CHECK(vectorsClose(s.find(b.id)->position,{110,20,0}));
});
test("parent rotational mate carries fixed child", [] {
    Scene s;auto a=makeBody(Shape::Box),g=makeBody(Shape::Group),b=makeBody(Shape::Sphere);
    a.fixed=true;a.rotation=QQuaternion::fromEulerAngles(90,0,0);b.parentId=g.id;b.position={0,0,10};b.fixed=true;
    s.bodies={a,g,b};s.mates<<mate(a,g,MateType::Parallel);
    CHECK(solve(s));CHECK(vectorsClose(s.find(b.id)->position,{0,-10,0}));
});
test("hierarchical bearing example loads", [] {
    Scene s;CHECK(loadProject("examples/mechanical/hierarchy-bearing.afcad",s));
    CHECK(s.bodies.size()==17);CHECK(s.find("rolling-elements")->parentId=="bearing-assembly");
    int children=0;for(const auto &b:s.bodies)if(s.descendantOf(b.id,"rolling-elements"))++children;
    CHECK(children==10);
});
test("hierarchy cycles dangling and mate cycles rejected", [] {
    Scene s;auto g=makeBody(Shape::Group),b=makeBody(Shape::Box);b.parentId=g.id;s.bodies={g,b};
    s.find(g.id)->parentId=b.id;CHECK(!s.validate());
    s.find(g.id)->parentId="missing";CHECK(!s.validate());
    s.find(g.id)->parentId.clear();s.mates<<mate(b,g,MateType::Coincident);CHECK(!solve(s));
});
test("group floor mate uses subtree minimum", [] {
    Scene s;auto g=makeBody(Shape::Group),b=makeBody(Shape::Box);b.parentId=g.id;s.bodies={g,b};
    auto m=mate(g,b,MateType::Ground);m.source="@ground";m.target=g.id;m.offset=5;s.mates<<m;
    CHECK(solve(s));CHECK(std::abs(s.minimumZ(g.id)-5)<.001);
    CHECK(vectorsClose(s.find(b.id)->position,{0,0,20}));
    s.bodies.removeLast();CHECK(!solve(s));
});
test("hierarchy file version round trip and invalid load protection", [] {
    Scene s;auto g=makeBody(Shape::Group),b=makeBody(Shape::Sphere);b.parentId=g.id;s.bodies={b,g};
    auto data=serialize(s);CHECK(QJsonDocument::fromJson(data).object()["version"].toInt()==8);
    Scene restored;CHECK(deserialize(data,restored));CHECK(restored.find(b.id)->parentId==g.id);
    auto root=QJsonDocument::fromJson(data).object();root["version"]=6;
    auto before=serialize(restored);CHECK(!deserialize(QJsonDocument(root).toJson(),restored));CHECK(serialize(restored)==before);
});
test("parent hiding applies to bounds picking and export", [] {
    Scene s;auto g=makeBody(Shape::Group),b=makeBody(Shape::Box);b.parentId=g.id;g.visible=false;s.bodies={g,b};
    CHECK(!s.isVisible(b.id));CHECK(!sceneBounds(s).valid);CHECK(pick(s,{0,0,100},{0,0,-1}).id.isEmpty());
    QTemporaryDir dir;CHECK(!exportGlb(dir.path()+"/hidden.glb",s));
    s.find(g.id)->visible=true;CHECK(s.isVisible(b.id));
    CHECK(exportStep(dir.path()+"/group.step",s));QVector<Body> bodies;
    CHECK(importStep(dir.path()+"/group.step",bodies));CHECK(bodies.size()==1);
});
