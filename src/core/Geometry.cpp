#include "Geometry.h"
#include "Kernel.h"
#include <algorithm>
#include <cmath>
namespace af {
void Bounds::include(const QVector3D &p) {
    if (!valid) {
        min = max = p;
        valid = true;
        return;
    }
    for (int i = 0; i < 3; ++i) {
        min[i] = qMin(min[i], p[i]);
        max[i] = qMax(max[i], p[i]);
    }
}
Mesh tessellate(const Body &body) {
    return solidData(body)->mesh;
}
Bounds sceneBounds(const Scene &scene) {
    Bounds b;
    for (const auto &body : scene.bodies)
        if (body.visible)
            for (const auto &v : tessellate(body).vertices)
                b.include(body.matrix().map(v.position));
    return b;
}
Hit pick(const Scene &scene, const QVector3D &origin, const QVector3D &direction) {
    Hit hit;
    const QVector3D dir = direction.normalized();
    if (dir.isNull())
        return hit;
    for (const auto &b : scene.bodies) {
        if (!b.visible)
            continue;
        const QMatrix4x4 matrix = b.matrix();
        const auto mesh = tessellate(b);
        for (int i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            auto a = matrix.map(mesh.vertices[i].position),
                 e1 = matrix.map(mesh.vertices[i + 1].position) - a,
                 e2 = matrix.map(mesh.vertices[i + 2].position) - a;
            auto p = QVector3D::crossProduct(dir, e2);
            float det = QVector3D::dotProduct(e1, p);
            if (std::abs(det) < 1e-8f)
                continue;
            float inv = 1 / det;
            auto t = origin - a;
            float u = QVector3D::dotProduct(t, p) * inv;
            if (u < 0 || u > 1)
                continue;
            auto q = QVector3D::crossProduct(t, e1);
            float v = QVector3D::dotProduct(dir, q) * inv;
            if (v < 0 || u + v > 1)
                continue;
            float d = QVector3D::dotProduct(e2, q) * inv;
            if (d > 0 && d < hit.distance) {
                hit.id = b.id;
                hit.distance = d;
                hit.point = origin + dir * d;
            }
        }
    }
    return hit;
}
} // namespace af
