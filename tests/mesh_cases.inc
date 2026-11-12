test("GLB roundtrip preserves visible names colors geometry and units", [] {
    QTemporaryDir d;
    Scene s;
    auto b = makeBody(Shape::Box);
    b.name = QStringLiteral("中文 底座");
    b.color = QColor("#39a6d2");
    b.position = {123, -45, 67};
    b.rotation = QQuaternion::fromEulerAngles(17, 31, 9);
    s.bodies << b;
    auto hidden = makeBody(Shape::Sphere);
    hidden.visible = false;
    s.bodies << hidden;
    QString why, p = d.path() + QStringLiteral("/中文.glb");
    CHECK(exportGlb(p, s, &why));
    QVector<Body> loaded;
    CHECK(importMeshFile(p, loaded, &why));
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].shape == Shape::Mesh);
    CHECK(loaded[0].name == b.name);
    CHECK(loaded[0].color == b.color);
    CHECK(std::abs(surfaceArea(loaded[0]) - surfaceArea(b)) < .1);
    auto original = tessellate(b);
    CHECK(original.vertices.size() == tessellate(loaded[0]).vertices.size());
    for (int i = 0; i < original.vertices.size(); ++i)
        CHECK(vectorsClose(b.matrix().map(original.vertices[i].position),
                           meshfixture::worldPoint(loaded[0], i), .001f));
});
test("GLB without scene list discovers root nodes", [] {
    QTemporaryDir d;
    QByteArray bin;
    auto root = meshfixture::root(bin);
    root.remove("scenes");
    root.remove("scene");
    auto path = d.path() + "/roots.glb";
    meshfixture::write(path, meshfixture::glb(root, bin));
    QVector<Body> out;
    CHECK(importMeshFile(path, out));
    CHECK(out.size() == 1);
});
test("OBJ handles UTF-8 BOM continuations and late material library", [] {
    QTemporaryDir d;
    meshfixture::write(d.path() + "/color.mtl", "newmtl tint\nKd 0 1 0\n");
    meshfixture::write(d.path() + "/late.obj",
                       QByteArray::fromHex("efbbbf") +
                           "v 0 0 0\nv 10 0 0\nv 0 10 0\nusemtl tint\nf 1 2 \\\n3\nmtllib color.mtl\n");
    QVector<Body> out;
    CHECK(importMeshFile(d.path() + "/late.obj", out));
    CHECK(out.size() == 1 && out[0].color == QColor(Qt::green));
});
test("mesh supports origin-axis mates and triangle picking", [] {
    Mesh mesh;
    mesh.vertices = {{{0, 0, 0}, {0, 0, 1}}, {{10, 0, 0}, {0, 0, 1}}, {{0, 20, 0}, {0, 0, 1}}};
    auto b = makeMeshBody("mesh", mesh);
    auto a = makeBody(Shape::Cylinder);
    a.fixed = true;
    a.visible = false;
    b.position = {20, 30, 40};
    Scene scene;
    scene.bodies = {a, b};
    scene.mates << mate(a, b, MateType::Coaxial);
    CHECK(solve(scene));
    CHECK(vectorsClose(scene.bodies[1].position, {0, 0, 40}));
    CHECK(pick(scene, {1, 1, 100}, {0, 0, -1}).id == b.id);
    Scene loaded;
    CHECK(deserialize(serialize(scene), loaded));
    CHECK(loaded.mates.size() == 1 && solve(loaded));
});
test("GLB independent fixture converts Y-up metres to Z-up millimetres", [] {
    QTemporaryDir d;
    QByteArray bin;
    auto root = meshfixture::root(bin);
    auto p = d.path() + "/triangle.glb";
    meshfixture::write(p, meshfixture::glb(root, bin));
    QVector<Body> bodies;
    CHECK(importMeshFile(p, bodies));
    CHECK(bodies.size() == 1);
    CHECK(vectorsClose(meshfixture::worldPoint(bodies[0], 0), {100, -300, 200}, .001f));
    CHECK(vectorsClose(meshfixture::worldPoint(bodies[0], 1), {120, -300, 200}, .001f));
    CHECK(vectorsClose(meshfixture::worldPoint(bodies[0], 2), {100, -300, 230}, .001f));
    CHECK(std::abs(meshfixture::area(bodies) - 300) < .01);
    CHECK(vectorsClose(tessellate(bodies[0]).vertices[0].normal, {0, -1, 0}));
});
test("GLB nested mirrored nonuniform transforms preserve winding", [] {
    QTemporaryDir d;
    QByteArray bin;
    auto root = meshfixture::root(bin);
    root["nodes"] =
        QJsonArray{QJsonObject{{"translation", QJsonArray{.1, .2, .3}}, {"children", QJsonArray{1}}},
                   QJsonObject{{"mesh", 0}, {"scale", QJsonArray{-1, 2, 3}}}};
    auto p = d.path() + "/mirror.glb";
    meshfixture::write(p, meshfixture::glb(root, bin));
    QVector<Body> out;
    CHECK(importMeshFile(p, out));
    CHECK(out.size() == 1);
    CHECK(std::abs(meshfixture::area(out) - 600) < .01);
    auto mesh = tessellate(out[0]);
    auto n = QVector3D::crossProduct(mesh.vertices[1].position - mesh.vertices[0].position,
                                     mesh.vertices[2].position - mesh.vertices[0].position)
                 .normalized();
    CHECK(QVector3D::dotProduct(n, mesh.vertices[0].normal) > .999f);
});
test("GLB node matrix uses column-major translation", [] {
    QTemporaryDir d;
    QByteArray bin;
    auto root = meshfixture::root(bin);
    root["nodes"] = QJsonArray{
        QJsonObject{{"mesh", 0}, {"matrix", QJsonArray{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, .1, .2, .3, 1}}}};
    auto p = d.path() + "/matrix.glb";
    meshfixture::write(p, meshfixture::glb(root, bin));
    QVector<Body> out;
    CHECK(importMeshFile(p, out));
    CHECK(vectorsClose(meshfixture::worldPoint(out[0], 0), {100, -300, 200}, .001f));
});
test("GLB supports interleaved positions and normals", [] {
    QTemporaryDir d;
    QByteArray bin;
    auto root = meshfixture::root(bin);
    bin.clear();
    for (auto p : {QVector3D(0, 0, 0), QVector3D(.02f, 0, 0), QVector3D(0, .03f, 0)})
        for (float f : {p.x(), p.y(), p.z(), 0.f, 0.f, 1.f})
            meshfixture::putFloat(bin, f);
    root["buffers"] = QJsonArray{QJsonObject{{"byteLength", bin.size()}}};
    root["bufferViews"] = QJsonArray{QJsonObject{{"buffer", 0}, {"byteLength", 72}, {"byteStride", 24}}};
    root["accessors"] = QJsonArray{
        QJsonObject{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
        QJsonObject{
            {"bufferView", 0}, {"byteOffset", 12}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}}};
    root["meshes"] = QJsonArray{
        QJsonObject{{"primitives",
                     QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 0}, {"NORMAL", 1}}}}}}}};
    auto p = d.path() + "/interleaved.glb";
    meshfixture::write(p, meshfixture::glb(root, bin));
    QVector<Body> out;
    CHECK(importMeshFile(p, out));
    CHECK(std::abs(meshfixture::area(out) - 300) < .01);
    CHECK(vectorsClose(tessellate(out[0]).vertices[0].normal, {0, -1, 0}));
});
for (int mode : {5, 6})
    test(QString("GLB triangle mode %1").arg(mode), [mode] {
        QTemporaryDir d;
        QByteArray bin;
        auto root = meshfixture::root(bin);
        auto meshes = root["meshes"].toArray();
        auto m = meshes[0].toObject();
        auto ps = m["primitives"].toArray();
        auto p = ps[0].toObject();
        p["mode"] = mode;
        ps[0] = p;
        m["primitives"] = ps;
        meshes[0] = m;
        root["meshes"] = meshes;
        auto path = d.path() + "/mode.glb";
        meshfixture::write(path, meshfixture::glb(root, bin));
        QVector<Body> out;
        CHECK(importMeshFile(path, out));
        CHECK(tessellate(out[0]).vertices.size() == 3);
    });
for (int fault = 0; fault < 8; ++fault)
    test(QString("GLB malformed or unsupported case %1 rolls back").arg(fault), [fault] {
        QTemporaryDir d;
        QByteArray bin;
        auto root = meshfixture::root(bin);
        if (fault == 0)
            root["extensionsRequired"] = QJsonArray{"KHR_draco_mesh_compression"};
        if (fault == 1) {
            auto views = root["bufferViews"].toArray();
            auto v = views[0].toObject();
            v["byteLength"] = 35;
            views[0] = v;
            root["bufferViews"] = views;
        }
        if (fault == 2)
            bin[38] = char(9);
        if (fault == 3) {
            auto nodes = root["nodes"].toArray();
            auto n = nodes[0].toObject();
            n["children"] = QJsonArray{0};
            nodes[0] = n;
            root["nodes"] = nodes;
        }
        if (fault == 4) {
            auto a = root["accessors"].toArray();
            auto p = a[0].toObject();
            p["sparse"] = QJsonObject{};
            a[0] = p;
            root["accessors"] = a;
        }
        if (fault == 5) {
            auto nodes = root["nodes"].toArray();
            auto n = nodes[0].toObject();
            n["skin"] = 0;
            nodes[0] = n;
            root["nodes"] = nodes;
        }
        if (fault == 6) {
            auto a = root["accessors"].toArray();
            auto p = a[0].toObject();
            p["count"] = 3000001;
            a[0] = p;
            root["accessors"] = a;
        }
        auto raw = meshfixture::glb(root, bin);
        if (fault == 7)
            raw.chop(1);
        auto path = d.path() + "/bad.glb";
        meshfixture::write(path, raw);
        QVector<Body> out{makeBody(Shape::Box)};
        auto id = out[0].id;
        QString why;
        CHECK(!importMeshFile(path, out, &why));
        CHECK(!why.isEmpty());
        CHECK(out.size() == 1 && out[0].id == id);
    });
test("OBJ supports negative indices normals groups and concave polygons", [] {
    QTemporaryDir d;
    auto path = d.path() + "/concave.obj";
    meshfixture::write(path, "o Concave\nv 0 0 0\nv 2 0 0\nv 2 2 0\nv 1 1 0\nv 0 2 0\nvn 0 0 1\nf -5//1 "
                             "-4//1 -3//1 -2//1 -1//1\n");
    QVector<Body> out;
    CHECK(importMeshFile(path, out));
    CHECK(out.size() == 1);
    CHECK(tessellate(out[0]).vertices.size() == 9);
    CHECK(std::abs(meshfixture::area(out) - 3) < 1e-6);
});
test("OBJ MTL roundtrip preserves names placement and colors", [] {
    QTemporaryDir d;
    Scene s;
    auto a = makeBody(Shape::Box);
    a.name = QStringLiteral("对象 一");
    a.position = {-30, 10, 5};
    a.color = QColor("#3299ef");
    s.bodies << a;
    auto b = makeBody(Shape::Cylinder);
    b.name = "second";
    b.color = QColor("#ec7632");
    b.rotation = QQuaternion::fromEulerAngles(5, 20, 12);
    s.bodies << b;
    auto p = d.path() + QStringLiteral("/场景.obj");
    QString why;
    CHECK(exportObj(p, s, &why));
    QVector<Body> out;
    CHECK(importMeshFile(p, out, &why));
    CHECK(out.size() == 2);
    CHECK(out[0].name == a.name && out[0].color == a.color);
    CHECK(out[1].name == b.name && out[1].color == b.color);
    CHECK(vectorsClose(meshfixture::worldPoint(out[0], 0), a.matrix().map(tessellate(a).vertices[0].position),
                       .001f));
    CHECK(std::abs(meshfixture::area(out) - surfaceArea(a) - surfaceArea(b)) <
          10); // cylinder mesh approximates analytic area
});
test("OBJ multiple materials split independently colored mesh objects", [] {
    QTemporaryDir d;
    meshfixture::write(d.path() + "/colors.mtl", "newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\n");
    meshfixture::write(
        d.path() + "/colors.obj",
        "mtllib colors.mtl\nv 0 0 0\nv 10 0 0\nv 0 10 0\nusemtl red\nf 1 2 3\nusemtl blue\nf 3 2 1\n");
    QVector<Body> out;
    CHECK(importMeshFile(d.path() + "/colors.obj", out));
    CHECK(out.size() == 2);
    CHECK(out[0].color == QColor(Qt::red) && out[1].color == QColor(Qt::blue));
});
for (const auto &face :
     {QByteArray("f 1 2 9"), QByteArray("f 0 2 3"), QByteArray("f 1 1 1"), QByteArray("f 1//3 2//3 3//3")})
    test("OBJ malformed face rejected: " + face, [face] {
        QTemporaryDir d;
        auto p = d.path() + "/bad.obj";
        meshfixture::write(p, QByteArray("v 0 0 0\nv 1 0 0\nv 0 1 0\n") + face + '\n');
        QVector<Body> out{makeBody(Shape::Box)};
        auto id = out[0].id;
        CHECK(!importMeshFile(p, out));
        CHECK(out.size() == 1 && out[0].id == id);
    });
test("mesh project version 3 roundtrip and old version rejection", [] {
    Mesh m;
    m.vertices = {{{0, 0, 0}, {0, 0, 1}}, {{10, 0, 0}, {0, 0, 1}}, {{0, 20, 0}, {0, 0, 1}}};
    Scene s;
    s.bodies << makeMeshBody("saved", m);
    s.bodies[0].position = {20, 30, 40};
    auto root = QJsonDocument::fromJson(serialize(s)).object();
    CHECK(root["version"].toInt() == 3);
    Scene loaded;
    CHECK(deserialize(serialize(s), loaded));
    CHECK(loaded.bodies[0].shape == Shape::Mesh);
    CHECK(vectorsClose(meshfixture::worldPoint(loaded.bodies[0], 2), {20, 50, 40}));
    CHECK(std::abs(surfaceArea(loaded.bodies[0]) - 100) < .001);
    root["version"] = 2;
    auto before = serialize(loaded);
    CHECK(!deserialize(QJsonDocument(root).toJson(), loaded));
    CHECK(serialize(loaded) == before);
});
test("corrupt mesh payload and decompression bomb header rejected", [] {
    Body b = makeBody(Shape::Box);
    CHECK(!decodeMesh("bad!", b));
    CHECK(b.shape == Shape::Box && !b.solid);
    CHECK(!decodeMesh(QByteArray::fromHex("ffffffff").toBase64(), b));
    QByteArray raw;
    meshfixture::put32(raw, 3);
    raw.append(72, '\0');
    CHECK(!decodeMesh(qCompress(raw).toBase64(), b));
    CHECK(b.shape == Shape::Box);
});
test("mesh decode checks remaining scene budget before decompression", [] {
    Mesh mesh;
    mesh.vertices = {{{0, 0, 0}, {0, 0, 1}}, {{10, 0, 0}, {0, 0, 1}}, {{0, 20, 0}, {0, 0, 1}}};
    const auto encoded = encodeMesh(makeMeshBody("mesh", mesh));
    auto original = makeBody(Shape::Box);
    CHECK(!decodeMesh(encoded, original, nullptr, 2));
    CHECK(original.shape == Shape::Box && !original.solid);
    CHECK(decodeMesh(encoded, original, nullptr, 3));
    CHECK(original.shape == Shape::Mesh);
});
test("mesh cannot participate in solid booleans or STEP export", [] {
    Mesh m;
    m.vertices = {{{0, 0, 0}, {0, 0, 1}}, {{10, 0, 0}, {0, 0, 1}}, {{0, 20, 0}, {0, 0, 1}}};
    auto b = makeMeshBody("mesh", m);
    Body result;
    QString why;
    CHECK(!booleanBody(b, makeBody(Shape::Box), BooleanOp::Fuse, result, &why));
    CHECK(why.contains(QStringLiteral("网格")));
    QTemporaryDir d;
    Scene s;
    s.bodies << b;
    CHECK(!exportStep(d.path() + "/mesh.step", s, &why));
});
test("empty export does not overwrite existing GLB or OBJ", [] {
    QTemporaryDir d;
    Scene s;
    auto b = makeBody(Shape::Box);
    b.visible = false;
    s.bodies << b;
    for (const auto &ext : {QString("glb"), QString("obj")}) {
        auto p = d.path() + "/keep." + ext;
        meshfixture::write(p, "keep");
        CHECK(!(ext == "glb" ? exportGlb(p, s) : exportObj(p, s)));
        QFile f(p);
        CHECK(f.open(QIODevice::ReadOnly));
        CHECK(f.readAll() == "keep");
    }
    CHECK(QDir(d.path()).entryList({"*.mtl"}, QDir::Files).isEmpty());
});
test("unsupported textures produce a visible import warning", [] {
    QTemporaryDir d;
    QByteArray bin;
    auto root = meshfixture::root(bin);
    root["materials"] = QJsonArray{
        QJsonObject{{"pbrMetallicRoughness", QJsonObject{{"baseColorTexture", QJsonObject{{"index", 0}}}}}}};
    auto meshes = root["meshes"].toArray();
    auto m = meshes[0].toObject();
    auto ps = m["primitives"].toArray();
    auto p = ps[0].toObject();
    p["material"] = 0;
    ps[0] = p;
    m["primitives"] = ps;
    meshes[0] = m;
    root["meshes"] = meshes;
    auto path = d.path() + "/textured.glb";
    meshfixture::write(path, meshfixture::glb(root, bin));
    QVector<Body> out;
    QStringList notes;
    CHECK(importMeshFile(path, out, nullptr, &notes));
    CHECK(!notes.isEmpty());
});
