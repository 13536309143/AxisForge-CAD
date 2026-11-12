#include "core/Constraints.h"
#include "core/Geometry.h"
#include "core/Kernel.h"
#include "core/ProjectIO.h"
#include "core/Scene.h"
#include <BRepCheck_Analyzer.hxx>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

#define CHECK(condition)                                                                                     \
    do {                                                                                                     \
        if (!(condition))                                                                                    \
            throw std::runtime_error(#condition);                                                            \
    } while (false)
using namespace af;
#include "MeshFixtures.h"
#include "StepFixtures.h"
namespace {
Mate mate(const Body &a, const Body &b, MateType type) {
    Mate m;
    m.id = QString::number(int(type)) + "-" + a.id;
    m.source = a.id;
    m.target = b.id;
    m.type = type;
    return m;
}
double meshVolume(const Mesh &mesh) {
    double v = 0;
    for (int i = 0; i < mesh.vertices.size(); i += 3)
        v += QVector3D::dotProduct(
                 mesh.vertices[i].position,
                 QVector3D::crossProduct(mesh.vertices[i + 1].position, mesh.vertices[i + 2].position)) /
             6.0;
    return v;
}
Scene pairScene() {
    Scene s;
    auto a = makeBody(Shape::Cylinder), b = makeBody(Shape::Cylinder, 2);
    a.fixed = true;
    b.position = {25, 12, 35};
    b.rotation = QQuaternion::fromEulerAngles(33, 27, 12);
    s.bodies = {a, b};
    return s;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    int passed = 0, failed = 0;
    auto test = [&](const QString &name, const std::function<void()> &fn) {
        try {
            fn();
            ++passed;
            out << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            ++failed;
            out << "FAIL " << name << ": " << e.what() << '\n';
        }
        out.flush();
    };
    for (int shape = 0; shape < 5; ++shape)
        test("mesh validity and analytic volume: " + shapeKey(Shape(shape)), [shape] {
            Body b = makeBody(Shape(shape));
            auto mesh = tessellate(b);
            CHECK(!mesh.vertices.isEmpty());
            CHECK(mesh.vertices.size() % 3 == 0);
            for (const auto &v : mesh.vertices) {
                CHECK(std::isfinite(v.position.x()) && std::isfinite(v.position.y()) &&
                      std::isfinite(v.position.z()));
                CHECK(std::abs(v.normal.length() - 1) < 0.001f);
            }
            for (int i = 0; i < mesh.vertices.size(); i += 3) {
                auto a = mesh.vertices[i], c = mesh.vertices[i + 1], d = mesh.vertices[i + 2];
                auto n = QVector3D::crossProduct(c.position - a.position, d.position - a.position);
                CHECK(n.lengthSquared() > 1e-12f);
                CHECK(QVector3D::dotProduct(n, a.normal + c.normal + d.normal) > 0);
            }
            CHECK(std::abs(meshVolume(mesh) - volume(b)) / volume(b) < 0.03);
            CHECK(surfaceArea(b) > 0);
        });
    test("box bounds use width X, depth Y and height Z", [] {
        Scene s;
        Body b = makeBody(Shape::Box);
        b.params.width = 10;
        b.params.depth = 20;
        b.params.height = 30;
        b.position = {5, 10, 15};
        s.bodies << b;
        auto bounds = sceneBounds(s);
        CHECK(vectorsClose(bounds.min, {0, 0, 0}));
        CHECK(vectorsClose(bounds.max, {10, 20, 30}));
    });
    test("transformed ray picks nearest visible body", [] {
        Scene s;
        auto a = makeBody(Shape::Box), b = makeBody(Shape::Sphere);
        a.rotation = QQuaternion::fromAxisAndAngle({0, 0, 1}, 25);
        b.position = {0, 0, -60};
        s.bodies = {a, b};
        CHECK(pick(s, {0, 0, 100}, {0, 0, -1}).id == a.id);
        s.bodies[0].visible = false;
        CHECK(pick(s, {0, 0, 100}, {0, 0, -1}).id == b.id);
        CHECK(pick(s, {500, 500, 100}, {0, 0, -1}).id.isEmpty());
    });
    test("coaxial aligns axis and removes radial offset", [] {
        auto s = pairScene();
        s.mates << mate(s.bodies[0], s.bodies[1], MateType::Coaxial);
        CHECK(solve(s));
        CHECK(residual(s, s.mates[0]).satisfied());
        CHECK(std::abs(s.bodies[1].position.z() - 35) < 0.001);
        CHECK(s.bodies[0].position.isNull());
    });
    test("parallel preserves position", [] {
        auto s = pairScene();
        auto position = s.bodies[1].position;
        s.mates << mate(s.bodies[0], s.bodies[1], MateType::Parallel);
        CHECK(solve(s));
        CHECK(residual(s, s.mates[0]).satisfied());
        CHECK(vectorsClose(s.bodies[1].position, position));
    });
    test("perpendicular handles initially parallel axes", [] {
        auto s = pairScene();
        s.bodies[1].rotation = QQuaternion();
        s.mates << mate(s.bodies[0], s.bodies[1], MateType::Perpendicular);
        CHECK(solve(s));
        CHECK(residual(s, s.mates[0]).satisfied());
    });
    test("unoriented antiparallel axes remain valid", [] {
        auto s = pairScene();
        s.bodies[1].rotation = QQuaternion::fromAxisAndAngle({1, 0, 0}, 180);
        auto before = s.bodies[1].rotation;
        s.mates << mate(s.bodies[0], s.bodies[1], MateType::Parallel);
        CHECK(solve(s));
        CHECK(std::abs(QQuaternion::dotProduct(before, s.bodies[1].rotation)) > 0.999f);
    });
    test("distance from coincident origins has deterministic direction", [] {
        auto s = pairScene();
        s.bodies[1].position = {};
        auto m = mate(s.bodies[0], s.bodies[1], MateType::Distance);
        m.distance = 40;
        s.mates << m;
        CHECK(solve(s));
        CHECK(vectorsClose(s.bodies[1].position, {0, 0, 40}));
    });
    test("zero center distance is supported", [] {
        auto s = pairScene();
        auto m = mate(s.bodies[0], s.bodies[1], MateType::Distance);
        m.distance = 0;
        s.mates << m;
        CHECK(solve(s));
        CHECK(s.bodies[1].position.isNull());
    });
    test("topological solve propagates reverse-stored chains", [] {
        auto s = pairScene();
        auto c = makeBody(Shape::Sphere);
        c.position = {77, 66, 90};
        s.bodies << c;
        s.mates << mate(s.bodies[1], c, MateType::Coaxial)
                << mate(s.bodies[0], s.bodies[1], MateType::Coaxial);
        CHECK(solve(s));
        s.bodies[0].position = {18, -9, 3};
        CHECK(solve(s));
        for (const auto &m : s.mates)
            CHECK(residual(s, m).satisfied());
        CHECK(std::abs(s.bodies[2].position.x() - 18) < 0.001);
    });
    test("compatible multiple constraints converge", [] {
        auto s = pairScene();
        auto a = mate(s.bodies[0], s.bodies[1], MateType::Coaxial);
        auto b = mate(s.bodies[0], s.bodies[1], MateType::Distance);
        b.distance = 50;
        s.mates = {a, b};
        CHECK(solve(s));
        for (const auto &m : s.mates)
            CHECK(residual(s, m).satisfied());
    });
    test("cycle rejected without partial mutation", [] {
        auto s = pairScene();
        s.mates = {mate(s.bodies[0], s.bodies[1], MateType::Parallel),
                   mate(s.bodies[1], s.bodies[0], MateType::Parallel)};
        auto before = serialize(s);
        QString why;
        CHECK(!solve(s, &why));
        CHECK(!why.isEmpty());
        CHECK(serialize(s) == before);
    });
    test("incompatible constraints roll back all transforms", [] {
        auto s = pairScene();
        auto c = makeBody(Shape::Box);
        c.position = {100, 0, 0};
        c.fixed = true;
        s.bodies << c;
        auto a = mate(s.bodies[0], s.bodies[1], MateType::Distance),
             b = mate(c, s.bodies[1], MateType::Distance);
        a.distance = b.distance = 1;
        s.mates = {a, b};
        auto before = serialize(s);
        CHECK(!solve(s));
        CHECK(serialize(s) == before);
    });
    test("fixed follower cannot be moved by solver", [] {
        auto s = pairScene();
        s.bodies[1].fixed = true;
        s.mates << mate(s.bodies[0], s.bodies[1], MateType::Coaxial);
        auto before = serialize(s);
        CHECK(!solve(s));
        CHECK(serialize(s) == before);
    });
    test("disabled cycle edges do not block solving", [] {
        auto s = pairScene();
        auto a = mate(s.bodies[0], s.bodies[1], MateType::Parallel),
             b = mate(s.bodies[1], s.bodies[0], MateType::Parallel);
        b.enabled = false;
        s.mates = {a, b};
        CHECK(solve(s));
    });
    test("duplicate IDs and invalid dimensions rejected", [] {
        Scene s;
        auto b = makeBody(Shape::Box);
        s.bodies = {b, b};
        CHECK(!s.validate());
        s.bodies.removeLast();
        s.bodies[0].params.width = -1;
        CHECK(!s.validate());
        s.bodies[0].params.width = 30;
        s.bodies[0].params.radius = std::numeric_limits<double>::quiet_NaN();
        CHECK(!s.validate());
    });
    test("self references and missing targets rejected", [] {
        auto s = pairScene();
        auto m = mate(s.bodies[0], s.bodies[0], MateType::Parallel);
        s.mates << m;
        CHECK(!solve(s));
        s.mates[0].target = "missing";
        CHECK(!solve(s));
    });
    test("torus tube cannot intersect its axis", [] {
        Scene s;
        auto b = makeBody(Shape::Torus);
        b.params.radius = 5;
        b.params.tubeRadius = 6;
        s.bodies << b;
        CHECK(!s.validate());
    });
    test("JSON preserves Unicode, colors, dimensions and mates", [] {
        auto s = demoScene();
        s.bodies[0].name = QStringLiteral("基座 · 测试 α");
        Scene loaded;
        QString why;
        CHECK(deserialize(serialize(s), loaded, &why));
        CHECK(serialize(s) == serialize(loaded));
    });
    test("malformed JSON leaves caller scene unchanged", [] {
        auto s = demoScene();
        auto before = serialize(s);
        CHECK(!deserialize("{broken", s));
        CHECK(serialize(s) == before);
    });
    test("future schema versions and wrong units rejected", [] {
        Scene s = demoScene();
        auto o = QJsonDocument::fromJson(serialize(s)).object();
        o["version"] = 99;
        CHECK(!deserialize(QJsonDocument(o).toJson(), s));
        o["version"] = 1;
        o["units"] = "inch";
        CHECK(!deserialize(QJsonDocument(o).toJson(), s));
    });
    test("wrong field types rejected rather than coerced", [] {
        Scene s = demoScene();
        auto o = QJsonDocument::fromJson(serialize(s)).object();
        auto bodies = o["bodies"].toArray();
        auto body = bodies[0].toObject();
        body["visible"] = "true";
        bodies[0] = body;
        o["bodies"] = bodies;
        CHECK(!deserialize(QJsonDocument(o).toJson(), s));
    });
    test("oversized input rejected before parsing", [] {
        Scene s;
        CHECK(!deserialize(QByteArray(64 * 1024 * 1024 + 1, ' '), s));
    });
    test("atomic save/load and exports contain transformed triangles", [] {
        QTemporaryDir dir;
        CHECK(dir.isValid());
        auto s = demoScene();
        QString why;
        CHECK(saveProject(dir.path() + "/part.afcad", s, &why));
        Scene loaded;
        CHECK(loadProject(dir.path() + "/part.afcad", loaded, &why));
        CHECK(serialize(s) == serialize(loaded));
        CHECK(exportMesh(dir.path() + "/part.obj", s, false, &why));
        CHECK(exportMesh(dir.path() + "/part.stl", s, true, &why));
        QFile obj(dir.path() + "/part.obj"), stl(dir.path() + "/part.stl");
        CHECK(obj.open(QIODevice::ReadOnly));
        CHECK(stl.open(QIODevice::ReadOnly));
        auto a = obj.readAll(), b = stl.readAll();
        CHECK(a.contains("v ") && a.contains("f "));
        CHECK(b.startsWith("solid AxisForge") && b.contains("facet normal") &&
              b.trimmed().endsWith("endsolid AxisForge"));
    });
    test("hidden bodies are excluded from mesh export", [] {
        QTemporaryDir dir;
        Scene s;
        auto b = makeBody(Shape::Box);
        b.visible = false;
        s.bodies << b;
        CHECK(exportMesh(dir.path() + "/hidden.obj", s, false));
        QFile f(dir.path() + "/hidden.obj");
        CHECK(f.open(QIODevice::ReadOnly));
        CHECK(!f.readAll().contains("\nv "));
    });
    test("failed save does not replace an existing file", [] {
        QTemporaryDir dir;
        QString p = dir.path() + "/keep.afcad";
        Scene s = demoScene();
        CHECK(saveProject(p, s));
        QFile f(p);
        CHECK(f.open(QIODevice::ReadOnly));
        auto bytes = f.readAll();
        f.close();
        s.bodies[0].params.width = -1;
        CHECK(!saveProject(p, s));
        CHECK(f.open(QIODevice::ReadOnly));
        CHECK(f.readAll() == bytes);
    });
    test("tessellation supports minimum and maximum subdivision", [] {
        for (int n : {8, 128})
            for (int i = 0; i < 5; ++i) {
                auto b = makeBody(Shape(i));
                b.params.segments = n;
                auto mesh = tessellate(b);
                CHECK(mesh.vertices.size() >= 12);
                CHECK(mesh.vertices.size() % 3 == 0);
            }
    });
#include "kernel_cases.inc"
#include "mesh_cases.inc"
    out << "\n" << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
