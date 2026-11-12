#include "Scene.h"
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <cmath>

namespace af {
namespace {
constexpr double pi = 3.14159265358979323846;
}
QString shapeKey(Shape s) {
    return QStringList{"box", "sphere", "cylinder", "cone", "torus"}.value(int(s));
}
QString shapeName(Shape s) {
    return QStringList{QStringLiteral("长方体"), QStringLiteral("球体"), QStringLiteral("圆柱"),
                       QStringLiteral("圆锥"), QStringLiteral("圆环")}
        .value(int(s));
}
QString mateKey(MateType t) {
    return QStringList{"coaxial", "parallel", "perpendicular", "distance"}.value(int(t));
}
QString mateName(MateType t) {
    return QStringList{QStringLiteral("同轴"), QStringLiteral("平行"), QStringLiteral("垂直"),
                       QStringLiteral("中心距离")}
        .value(int(t));
}
QMatrix4x4 Body::matrix() const {
    QMatrix4x4 m;
    m.translate(position);
    m.rotate(rotation);
    return m;
}
QVector3D Body::axis(int index) const {
    return rotation
        .rotatedVector(index == 0   ? QVector3D(1, 0, 0)
                       : index == 1 ? QVector3D(0, 1, 0)
                                    : QVector3D(0, 0, 1))
        .normalized();
}
Body *Scene::find(const QString &id) {
    for (auto &b : bodies)
        if (b.id == id)
            return &b;
    return nullptr;
}
const Body *Scene::find(const QString &id) const {
    for (const auto &b : bodies)
        if (b.id == id)
            return &b;
    return nullptr;
}
bool vectorsClose(const QVector3D &a, const QVector3D &b, float t) {
    return (a - b).length() <= t;
}
bool Scene::validate(QString *error) const {
    auto fail = [&](const QString &s) {
        if (error)
            *error = s;
        return false;
    };
    if (title.size() > 200 || bodies.size() > 300 || mates.size() > 1000)
        return fail(QStringLiteral("项目超过大小限制。"));
    QSet<QString> ids, mateIds;
    auto finite = [](double v) { return std::isfinite(v); };
    for (const auto &b : bodies) {
        if (b.id.isEmpty() || b.id.size() > 80 || ids.contains(b.id))
            return fail(QStringLiteral("模型 ID 缺失或重复。"));
        ids.insert(b.id);
        if (b.name.trimmed().isEmpty() || b.name.size() > 160 || int(b.shape) < 0 || int(b.shape) > 4 ||
            !b.color.isValid())
            return fail(QStringLiteral("模型名称、类型或颜色无效。"));
        for (double value :
             {b.params.width, b.params.height, b.params.depth, b.params.radius, b.params.tubeRadius})
            if (!finite(value) || value < 0.1 || value > 10000)
                return fail(QStringLiteral("尺寸必须在 0.1～10000 mm 之间。"));
        if (b.params.segments < 8 || b.params.segments > 128)
            return fail(QStringLiteral("细分数必须在 8～128 之间。"));
        if (b.shape == Shape::Torus && b.params.tubeRadius >= b.params.radius)
            return fail(QStringLiteral("圆环管半径必须小于主半径。"));
        for (float value : {b.position.x(), b.position.y(), b.position.z()})
            if (!finite(value) || std::abs(value) > 1000000)
                return fail(QStringLiteral("模型位置无效或超出范围。"));
        const auto q = b.rotation;
        for (float value : {q.scalar(), q.x(), q.y(), q.z()})
            if (!finite(value))
                return fail(QStringLiteral("旋转包含无效数值。"));
        if (std::abs(q.length() - 1.0f) > 0.002f)
            return fail(QStringLiteral("旋转四元数必须归一化。"));
    }
    for (const auto &m : mates) {
        if (m.id.isEmpty() || m.id.size() > 80 || mateIds.contains(m.id))
            return fail(QStringLiteral("约束 ID 缺失或重复。"));
        mateIds.insert(m.id);
        if (!ids.contains(m.source) || !ids.contains(m.target) || m.source == m.target)
            return fail(QStringLiteral("约束引用不存在的模型或自身。"));
        if (m.sourceAxis < 0 || m.sourceAxis > 2 || m.targetAxis < 0 || m.targetAxis > 2 || int(m.type) < 0 ||
            int(m.type) > 3)
            return fail(QStringLiteral("约束类型或轴无效。"));
        if (!finite(m.distance) || m.distance < 0 || m.distance > 1000000)
            return fail(QStringLiteral("约束距离无效。"));
    }
    return true;
}
Body makeBody(Shape shape, int ordinal) {
    Body b;
    b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    b.shape = shape;
    b.name = shapeName(shape) + QString(" %1").arg(ordinal);
    const QColor colors[] = {QColor("#67b8e7"), QColor("#e9af68"), QColor("#73d0b5"), QColor("#bd9be8"),
                             QColor("#e58fa7")};
    b.color = colors[int(shape)];
    return b;
}
Scene demoScene() {
    Scene s;
    s.title = QStringLiteral("精密转轴 · 装配示例");
    Body base = makeBody(Shape::Box);
    base.name = QStringLiteral("01 · 基座 / 固定");
    base.params.width = 100;
    base.params.depth = 68;
    base.params.height = 10;
    base.position = {0, 0, 5};
    base.fixed = true;
    base.color = QColor("#516780");
    // Box uses width=X, depth=Y, height=Z, matching the workbench's Z-up convention.
    Body shaft = makeBody(Shape::Cylinder);
    shaft.name = QStringLiteral("02 · 主轴 / 基准 Z");
    shaft.params.radius = 9;
    shaft.params.height = 62;
    shaft.position = {0, 0, 41};
    shaft.color = QColor("#65c8da");
    Body collar = makeBody(Shape::Torus);
    collar.name = QStringLiteral("03 · 定位环 / 同轴");
    collar.params.radius = 15;
    collar.params.tubeRadius = 4;
    collar.position = {0, 0, 38};
    Body ball = makeBody(Shape::Sphere);
    ball.name = QStringLiteral("04 · 球头");
    ball.params.radius = 14;
    ball.position = {0, 0, 83};
    ball.color = QColor("#efba74");
    Body cone = makeBody(Shape::Cone);
    cone.name = QStringLiteral("05 · 定位锥");
    cone.params.radius = 12;
    cone.params.height = 28;
    cone.position = {34, 12, 24};
    s.bodies = {base, shaft, collar, ball, cone};
    Mate a;
    a.id = "demo-coaxial";
    a.source = shaft.id;
    a.target = collar.id;
    a.type = MateType::Coaxial;
    s.mates.push_back(a);
    Mate d;
    d.id = "demo-distance";
    d.source = shaft.id;
    d.target = ball.id;
    d.type = MateType::Distance;
    d.distance = 42;
    s.mates.push_back(d);
    return s;
}
double volume(const Body &b) {
    const auto &p = b.params;
    switch (b.shape) {
    case Shape::Box:
        return p.width * p.depth * p.height;
    case Shape::Sphere:
        return 4 * pi * p.radius * p.radius * p.radius / 3;
    case Shape::Cylinder:
        return pi * p.radius * p.radius * p.height;
    case Shape::Cone:
        return pi * p.radius * p.radius * p.height / 3;
    case Shape::Torus:
        return 2 * pi * pi * p.radius * p.tubeRadius * p.tubeRadius;
    }
    return 0;
}
double surfaceArea(const Body &b) {
    const auto &p = b.params;
    switch (b.shape) {
    case Shape::Box:
        return 2 * (p.width * p.depth + p.width * p.height + p.depth * p.height);
    case Shape::Sphere:
        return 4 * pi * p.radius * p.radius;
    case Shape::Cylinder:
        return 2 * pi * p.radius * (p.radius + p.height);
    case Shape::Cone:
        return pi * p.radius * (p.radius + std::hypot(p.radius, p.height));
    case Shape::Torus:
        return 4 * pi * pi * p.radius * p.tubeRadius;
    }
    return 0;
}
} // namespace af
