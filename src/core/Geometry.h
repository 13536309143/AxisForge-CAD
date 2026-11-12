#pragma once
#include "Scene.h"
#include <limits>
namespace af {
struct Vertex {
    QVector3D position, normal;
};
static_assert(sizeof(Vertex) == 6 * sizeof(float), "GPU vertex layout must be tightly packed");
struct Mesh {
    QVector<Vertex> vertices;
};
struct Bounds {
    QVector3D min, max;
    bool valid = false;
    void include(const QVector3D &p);
    QVector3D center() const {
        return (min + max) * 0.5f;
    }
    float radius() const {
        return (max - min).length() * 0.5f;
    }
};
Mesh tessellate(const Body &body);
Bounds sceneBounds(const Scene &scene);
struct Hit {
    QString id;
    float distance = std::numeric_limits<float>::max();
    QVector3D point;
};
Hit pick(const Scene &scene, const QVector3D &origin, const QVector3D &direction);
} // namespace af
