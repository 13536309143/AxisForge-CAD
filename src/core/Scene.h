#pragma once
#include <QColor>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QVector3D>
#include <QVector>

namespace af {
enum class Shape { Box, Sphere, Cylinder, Cone, Torus };
enum class MateType { Coaxial, Parallel, Perpendicular, Distance };
QString shapeKey(Shape shape);
QString shapeName(Shape shape);
QString mateKey(MateType type);
QString mateName(MateType type);
struct Parameters {
    double width = 30, height = 30, depth = 30, radius = 15, tubeRadius = 5;
    int segments = 48;
};
struct Body {
    QString id, name;
    Shape shape = Shape::Box;
    Parameters params;
    QVector3D position;
    QQuaternion rotation;
    QColor color = QColor("#62b6e8");
    bool visible = true, fixed = false;
    QMatrix4x4 matrix() const;
    QVector3D axis(int index) const;
};
struct Mate {
    QString id, source, target;
    MateType type = MateType::Coaxial;
    int sourceAxis = 2, targetAxis = 2;
    double distance = 30;
    bool enabled = true;
};
struct Scene {
    QString title = QStringLiteral("未命名装配");
    QVector<Body> bodies;
    QVector<Mate> mates;
    Body *find(const QString &id);
    const Body *find(const QString &id) const;
    bool validate(QString *error = nullptr) const;
};
Body makeBody(Shape shape, int ordinal = 1);
Scene demoScene();
double volume(const Body &body);
double surfaceArea(const Body &body);
bool vectorsClose(const QVector3D &a, const QVector3D &b, float tolerance = 1e-4f);
} // namespace af
