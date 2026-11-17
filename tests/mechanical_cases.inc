for(const auto &entry:QVector<QPair<QString,int>>{{"bearing",14},{"gear",2},{"linear-guide",4}}) {
    test(("mechanical example " + entry.first).toStdString().c_str(), [&] {
        Scene example;QString error;
        CHECK(loadProject("examples/mechanical/"+entry.first+".afcad",example,&error));
        CHECK(example.bodies.size()==entry.second);
        CHECK(example.validate(&error));
        for(const auto &body:example.bodies) CHECK(volume(body)>0);
        Scene again;CHECK(deserialize(serialize(example),again,&error));
        CHECK(again.bodies.size()==example.bodies.size());
        QVector<Body> step,mesh;
        CHECK(importStep("examples/mechanical/"+entry.first+".step",step,&error));
        CHECK(step.size()==example.bodies.size());
        CHECK(importMeshFile("examples/mechanical/"+entry.first+".glb",mesh,&error));
        CHECK(mesh.size()==example.bodies.size());
    });
}
