test("current format requires explicit parent identity", [] {
    Scene scene=demoScene();auto before=serialize(scene);
    auto root=QJsonDocument::fromJson(before).object();auto bodies=root["bodies"].toArray();
    auto first=bodies[0].toObject();first.remove("parentId");bodies[0]=first;root["bodies"]=bodies;
    CHECK(!deserialize(QJsonDocument(root).toJson(),scene));CHECK(serialize(scene)==before);
});
test("current format requires angle and offset on every mate", [] {
    Scene scene=demoScene();auto before=serialize(scene);
    for(const auto &key:QStringList{"angle","offset"}) {
        auto root=QJsonDocument::fromJson(before).object();auto mates=root["mates"].toArray();
        auto first=mates[0].toObject();first.remove(key);mates[0]=first;root["mates"]=mates;
        CHECK(!deserialize(QJsonDocument(root).toJson(),scene));CHECK(serialize(scene)==before);
    }
});
