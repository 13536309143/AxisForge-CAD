test("OCCT primitives are valid exact solids", [] {
    for (int i = 0; i < 5; ++i) {
        auto b = makeBody(Shape(i));
        auto data = solidData(b);
        CHECK(data->solids == 1);
        CHECK(data->faces > 0);
        CHECK(BRepCheck_Analyzer(data->shape).IsValid());
    }
    auto sphere = makeBody(Shape::Sphere);
    CHECK(std::abs(volume(sphere) - 4.0 * 3.141592653589793 * 3375.0 / 3) < 1e-5);
});
test("OCCT primitive parameter edits invalidate only geometry cache", [] {
    auto b = makeBody(Shape::Box);
    auto original = solidData(b);
    Body copy = b;
    copy.params.width = 60;
    auto changed = solidData(copy);
    CHECK(original != changed);
    CHECK(std::abs(changed->volume - 54000) < 1e-6);
    CHECK(solidData(b) == original);
    CHECK(std::abs(volume(b) - 27000) < 1e-6);
});
for (int operation = 0; operation < 3; ++operation)
    test(QString("OCCT boolean volume %1").arg(operation), [operation] {
        auto a = makeBody(Shape::Box), b = makeBody(Shape::Box, 2);
        a.params.width = a.params.depth = a.params.height = 20;
        b.params = a.params;
        b.position = {10, 0, 0};
        Body result;
        QString error;
        CHECK(booleanBody(a, b, BooleanOp(operation), result, &error));
        CHECK(result.shape == Shape::Solid);
        CHECK(std::abs(volume(result) - (operation == 0 ? 12000 : 4000)) < 1e-5);
        CHECK(BRepCheck_Analyzer(result.solid->shape).IsValid());
        CHECK(!tessellate(result).vertices.isEmpty());
        CHECK(std::abs(volume(a) - 8000) < 1e-5);
    });
test("display bounds use OCCT geometry rather than coarse mesh", [] {
    auto sphere = makeBody(Shape::Sphere);
    sphere.params.segments = 8;
    auto data = solidData(sphere);
    CHECK(data->bounds.valid);
    CHECK(vectorsClose(data->bounds.min, {-15, -15, -15}, 0.001f));
    CHECK(vectorsClose(data->bounds.max, {15, 15, 15}, 0.001f));
});
test("OCCT through hole removes cylinder volume", [] {
    auto block = makeBody(Shape::Box), drill = makeBody(Shape::Cylinder);
    block.params.width = block.params.depth = 40;
    block.params.height = 20;
    drill.params.radius = 5;
    drill.params.height = 40;
    Body result;
    CHECK(booleanBody(block, drill, BooleanOp::Cut, result));
    CHECK(std::abs(volume(result) - (32000 - 3.141592653589793 * 25 * 20)) < 1e-5);
});
test("boolean placement preserves world coordinates", [] {
    auto a = makeBody(Shape::Box), b = makeBody(Shape::Box);
    a.params.width = a.params.depth = a.params.height = 20;
    b.params = a.params;
    a.position = {100, 50, 20};
    a.rotation = QQuaternion::fromAxisAndAngle({0, 0, 1}, 90);
    b.position = {110, 50, 20};
    Body result;
    CHECK(booleanBody(a, b, BooleanOp::Common, result));
    CHECK(std::abs(volume(result) - 4000) < 0.01);
    CHECK(vectorsClose(result.position, a.position));
});
test("empty common and complete subtraction fail transactionally", [] {
    auto a = makeBody(Shape::Box), b = makeBody(Shape::Box);
    b.position = {100, 0, 0};
    Body result = makeBody(Shape::Sphere);
    const auto id = result.id;
    CHECK(!booleanBody(a, b, BooleanOp::Common, result));
    CHECK(result.id == id);
    b.position = {};
    CHECK(!booleanBody(a, b, BooleanOp::Cut, result));
    CHECK(result.id == id);
    CHECK(!booleanBody(a, a, BooleanOp::Fuse, result));
});
test("multiple-solid union is retained", [] {
    auto a = makeBody(Shape::Box), b = makeBody(Shape::Box);
    b.position = {100, 0, 0};
    Body result;
    CHECK(booleanBody(a, b, BooleanOp::Fuse, result));
    CHECK(result.solid->solids == 2);
    CHECK(std::abs(volume(result) - 54000) < 1e-5);
});
test("BRep project save reload and repeat boolean", [] {
    auto a = makeBody(Shape::Box), b = makeBody(Shape::Sphere);
    Body result;
    CHECK(booleanBody(a, b, BooleanOp::Cut, result));
    Scene s;
    s.bodies << result;
    auto bytes = serialize(s);
    Scene loaded;
    QString error;
    CHECK(deserialize(bytes, loaded, &error));
    CHECK(loaded.bodies[0].solid);
    CHECK(std::abs(volume(loaded.bodies[0]) - volume(result)) < 1e-5);
    b.position = {30, 0, 0};
    Body again;
    CHECK(booleanBody(loaded.bodies[0], b, BooleanOp::Fuse, again));
    CHECK(volume(again) > volume(result));
});
test("bad BRep payload leaves body unchanged", [] {
    auto b = makeBody(Shape::Box);
    CHECK(!decodeBrep("not-base64!!", b));
    CHECK(b.shape == Shape::Box);
    CHECK(!b.solid);
    CHECK(!decodeBrep(QByteArray::fromHex("ffffffff").toBase64(), b));
});
test("legacy project version 1 remains readable", [] {
    Scene s;
    s.bodies << makeBody(Shape::Box);
    auto root = QJsonDocument::fromJson(serialize(s)).object();
    root["version"] = 1;
    Scene loaded;
    CHECK(deserialize(QJsonDocument(root).toJson(), loaded));
    CHECK(std::abs(volume(loaded.bodies[0]) - 27000) < 1e-6);
});
test("STEP exchange preserves multiple solids names colors and placement", [] {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    Scene s;
    auto a = makeBody(Shape::Box), b = makeBody(Shape::Sphere);
    a.name = QStringLiteral("中文基座 α");
    a.color = QColor("#32a4d2");
    a.position = {20, 30, 40};
    b.position = {-50, 0, 0};
    s.bodies = {a, b};
    QString path = directory.path() + QStringLiteral("/交换样例.step"), error;
    CHECK(exportStep(path, s, &error));
    QVector<Body> imported;
    CHECK(importStep(path, imported, &error));
    CHECK(imported.size() == 2);
    double sum = 0;
    bool found = false;
    for (const auto &body : imported) {
        sum += volume(body);
        if (body.name == a.name) {
            found = true;
            CHECK(std::abs(body.color.red() - a.color.red()) <= 1);
            Scene one;
            one.bodies << body;
            auto bounds = sceneBounds(one);
            CHECK(vectorsClose(bounds.center(), a.position, 0.01f));
        }
    }
    CHECK(found);
    CHECK(std::abs(sum - volume(a) - volume(b)) < 0.01);
});
test("STEP roundtrip of boolean result remains exact", [] {
    QTemporaryDir directory;
    auto a = makeBody(Shape::Box), b = makeBody(Shape::Cylinder);
    b.params.radius = 5;
    b.params.height = 50;
    Body cut;
    CHECK(booleanBody(a, b, BooleanOp::Cut, cut));
    Scene s;
    s.bodies << cut;
    CHECK(exportStep(directory.path() + "/cut.stp", s));
    QVector<Body> imported;
    CHECK(importStep(directory.path() + "/cut.stp", imported));
    CHECK(imported.size() == 1);
    CHECK(std::abs(volume(imported[0]) - volume(cut)) < 0.01);
});
test("STEP export excludes hidden and rejects empty scene", [] {
    QTemporaryDir directory;
    Scene s;
    auto a = makeBody(Shape::Box), b = makeBody(Shape::Sphere);
    b.visible = false;
    s.bodies = {a, b};
    CHECK(exportStep(directory.path() + "/visible.step", s));
    QVector<Body> imported;
    CHECK(importStep(directory.path() + "/visible.step", imported));
    CHECK(imported.size() == 1);
    s.bodies[0].visible = false;
    CHECK(!exportStep(directory.path() + "/empty.step", s));
});
test("malformed STEP import leaves destination untouched", [] {
    QTemporaryDir directory;
    QFile f(directory.path() + "/bad.step");
    CHECK(f.open(QIODevice::WriteOnly));
    f.write("not a STEP file");
    f.close();
    QVector<Body> imported{makeBody(Shape::Box)};
    auto id = imported[0].id;
    CHECK(!importStep(f.fileName(), imported));
    CHECK(imported.size() == 1 && imported[0].id == id);
});
test("STEP nested repeated instances retain composed placement", [] {
    QTemporaryDir dir;
    QString path = dir.path() + "/nested.step";
    CHECK(writeNestedFixture(path));
    QVector<Body> imported;
    QString error;
    CHECK(importStep(path, imported, &error));
    CHECK(imported.size() == 2);
    for (const auto &b : imported) {
        Scene s;
        s.bodies << b;
        auto center = sceneBounds(s).center();
        CHECK(std::abs(center.x() - 105) < 0.01);
        CHECK(std::abs(center.y()) < 0.01 || std::abs(center.y() - 30) < 0.01);
        CHECK(std::abs(volume(b) - 8000) < 0.01);
        CHECK(b.name.contains("Instance"));
    }
    CHECK(imported[0].id != imported[1].id);
    CHECK(!vectorsClose(imported[0].position, imported[1].position));
});
test("STEP source metres normalize to project millimetres", [] {
    QTemporaryDir dir;
    Scene s;
    s.bodies << makeBody(Shape::Box);
    QString path = dir.path() + "/units.step";
    CHECK(exportStep(path, s));
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    auto bytes = file.readAll();
    file.close();
    CHECK(bytes.contains("SI_UNIT(.MILLI.,.METRE.)"));
    bytes.replace("SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT($,.METRE.)");
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    QVector<Body> imported;
    CHECK(importStep(path, imported));
    CHECK(imported.size() == 1);
    CHECK(std::abs(volume(imported[0]) / volume(s.bodies[0]) - 1e9) < 10);
});
