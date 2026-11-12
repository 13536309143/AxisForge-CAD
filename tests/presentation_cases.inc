for (auto kind : {MateType::Coincident, MateType::Angle, MateType::AxialOffset})
    test("extended mate solves: " + mateKey(kind), [kind] {
        auto scene = pairScene();
        auto relation = mate(scene.bodies[0], scene.bodies[1], kind);
        relation.angle = 37;
        relation.offset = -23;
        const auto oldPosition = scene.bodies[1].position;
        scene.mates << relation;
        CHECK(solve(scene));
        CHECK(residual(scene, relation).satisfied());
        if (kind == MateType::Coincident)
            CHECK(vectorsClose(scene.bodies[0].position, scene.bodies[1].position));
        if (kind == MateType::Angle)
            CHECK(vectorsClose(scene.bodies[1].position, oldPosition));
        if (kind == MateType::AxialOffset) {
            CHECK(std::abs(scene.bodies[1].position.x() - oldPosition.x()) < .001);
            CHECK(std::abs(scene.bodies[1].position.y() - oldPosition.y()) < .001);
        }
    });
for (double angle : {0.0, 0.01, 30.0, 90.0, 179.99, 180.0})
    test(QString("directed angle nearGrid boundary %1").arg(angle), [angle] {
        auto scene = pairScene();
        auto m = mate(scene.bodies[0], scene.bodies[1], MateType::Angle);
        m.angle = angle;
        scene.mates << m;
        CHECK(solve(scene));
        CHECK(residual(scene, m).satisfied());
    });
test("coaxial plus signed axial offset determines origin along source axis", [] {
    auto scene = pairScene();
    auto coax = mate(scene.bodies[0], scene.bodies[1], MateType::Coaxial);
    auto offset = mate(scene.bodies[0], scene.bodies[1], MateType::AxialOffset);
    offset.offset = -40;
    scene.mates = {coax, offset};
    CHECK(solve(scene));
    CHECK(vectorsClose(scene.bodies[1].position, {0, 0, -40}));
});
test("extended constraint conflict rolls back complete scene", [] {
    auto scene = pairScene();
    auto m = mate(scene.bodies[0], scene.bodies[1], MateType::Coincident);
    auto d = mate(scene.bodies[0], scene.bodies[1], MateType::Distance);
    d.distance = 10;
    scene.mates = {m, d};
    auto before = serialize(scene);
    CHECK(!solve(scene));
    CHECK(serialize(scene) == before);
});
test("version 4 preserves angle offset and enabled state", [] {
    auto scene = pairScene();
    auto m = mate(scene.bodies[0], scene.bodies[1], MateType::Angle);
    m.angle = 42.5;
    m.offset = -15;
    m.enabled = false;
    scene.mates << m;
    auto root = QJsonDocument::fromJson(serialize(scene)).object();
    CHECK(root["version"].toInt() == 4);
    Scene loaded;
    CHECK(deserialize(serialize(scene), loaded));
    CHECK(loaded.mates[0].angle == 42.5 && loaded.mates[0].offset == -15 && !loaded.mates[0].enabled);
    root["version"] = 3;
    CHECK(!deserialize(QJsonDocument(root).toJson(), loaded));
});
test("invalid extended mate parameters are rejected", [] {
    auto scene = pairScene();
    auto m = mate(scene.bodies[0], scene.bodies[1], MateType::Angle);
    scene.mates << m;
    scene.mates[0].angle = 181;
    CHECK(!scene.validate());
    scene.mates[0].angle = 90;
    scene.mates[0].offset = std::numeric_limits<double>::infinity();
    CHECK(!scene.validate());
});
for (int type = 0; type < 7; ++type)
    test("mate guide geometry and metadata: " + mateKey(MateType(type)), [type] {
        auto scene = pairScene();
        auto m = mate(scene.bodies[0], scene.bodies[1], MateType(type));
        m.angle = 45;
        m.offset = 25;
        scene.mates << m;
        CHECK(solve(scene));
        auto guide = mateGuide(scene, m);
        CHECK(guide.valid && guide.visible && guide.satisfied && guide.enabled);
        CHECK(!guide.lines.isEmpty() && !guide.labels.isEmpty());
        CHECK(guide.source == m.source && guide.target == m.target);
        for (const auto &line : guide.lines)
            for (const auto &v : {line.a, line.b})
                CHECK(std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z()));
    });
test("hidden disabled and missing mates keep honest annotations", [] {
    auto scene = pairScene();
    auto m = mate(scene.bodies[0], scene.bodies[1], MateType::Distance);
    m.enabled = false;
    auto guide = mateGuide(scene, m);
    CHECK(guide.valid && !guide.enabled && !guide.satisfied);
    for (const auto &line : guide.lines)
        CHECK(line.dashed);
    scene.bodies[1].visible = false;
    guide = mateGuide(scene, m);
    CHECK(guide.valid && !guide.visible && guide.lines.isEmpty());
    m.target = "missing";
    CHECK(!mateGuide(scene, m).valid);
});
test("adaptive grid covers large transformed geometry with bounded density", [] {
    Scene scene;
    auto b = makeBody(Shape::Box);
    b.params.width = 10000;
    b.params.depth = 8000;
    b.position = {45000, -30000, 10};
    b.rotation = QQuaternion::fromEulerAngles(0, 0, 37);
    scene.bodies << b;
    auto bounds = visibleWorldBounds(scene);
    auto grid = adaptiveGrid(bounds, b.position, 20000);
    CHECK(grid.minX <= bounds.min.x() && grid.maxX >= bounds.max.x());
    CHECK(grid.minY <= bounds.min.y() && grid.maxY >= bounds.max.y());
    CHECK(grid.z < bounds.min.z());
    CHECK((grid.maxX - grid.minX) / grid.step <= 42 && (grid.maxY - grid.minY) / grid.step <= 42);
    CHECK(grid.step > 10);
});
test("grid follows visibility camera range and empty scene", [] {
    Scene scene;
    auto b = makeBody(Shape::Box);
    scene.bodies << b;
    auto bounds = visibleWorldBounds(scene);
    auto nearGrid = adaptiveGrid(bounds, {0, 0, 0}, 50), farGrid = adaptiveGrid(bounds, {0, 0, 0}, 20000);
    CHECK(farGrid.step > nearGrid.step && farGrid.maxX > nearGrid.maxX);
    scene.bodies[0].visible = false;
    CHECK(!visibleWorldBounds(scene).valid);
    auto empty = adaptiveGrid({}, {0, 0, 0}, 180);
    CHECK(std::isfinite(empty.step) && empty.step > 0);
    CHECK(empty == adaptiveGrid({}, {0, 0, 0}, 180));
});
