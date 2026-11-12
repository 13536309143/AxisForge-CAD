#include "Geometry.h"
#include <algorithm>
#include <cmath>
namespace af {
namespace {
constexpr float pi = 3.14159265358979323846f;
void tri(Mesh &m, Vertex a, Vertex b, Vertex c) {
    const QVector3D cross = QVector3D::crossProduct(b.position - a.position, c.position - a.position);
    if (cross.lengthSquared() < 1e-12f)
        return;
    if (QVector3D::dotProduct(cross, a.normal + b.normal + c.normal) < 0)
        std::swap(b, c);
    m.vertices << a << b << c;
}
void quad(Mesh &m, Vertex a, Vertex b, Vertex c, Vertex d) {
    tri(m, a, b, c);
    tri(m, a, c, d);
}
} // namespace
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
Mesh tessellate(const Body &b) {
    Mesh mesh;
    const auto &p = b.params;
    const int n = p.segments;
    const int rings = qMax(8, n / 2);
    const float r = float(p.radius), h = float(p.height), tube = float(p.tubeRadius);
    if (b.shape == Shape::Box) {
        const float x = float(p.width) / 2, y = float(p.depth) / 2, z = h / 2;
        auto face = [&](QVector3D a, QVector3D c, QVector3D d, QVector3D e, QVector3D normal) {
            quad(mesh, {a, normal}, {c, normal}, {d, normal}, {e, normal});
        };
        face({-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z}, {0, 0, 1});
        face({x, -y, -z}, {-x, -y, -z}, {-x, y, -z}, {x, y, -z}, {0, 0, -1});
        face({x, -y, z}, {x, -y, -z}, {x, y, -z}, {x, y, z}, {1, 0, 0});
        face({-x, -y, -z}, {-x, -y, z}, {-x, y, z}, {-x, y, -z}, {-1, 0, 0});
        face({-x, y, z}, {x, y, z}, {x, y, -z}, {-x, y, -z}, {0, 1, 0});
        face({-x, -y, -z}, {x, -y, -z}, {x, -y, z}, {-x, -y, z}, {0, -1, 0});
    } else if (b.shape == Shape::Sphere) {
        auto point = [&](int row, int col) {
            float t = pi * row / rings, a = 2 * pi * col / n;
            QVector3D normal(std::sin(t) * std::cos(a), std::sin(t) * std::sin(a), std::cos(t));
            return Vertex{normal * r, normal};
        };
        for (int j = 0; j < rings; ++j)
            for (int i = 0; i < n; ++i)
                quad(mesh, point(j, i), point(j + 1, i), point(j + 1, i + 1), point(j, i + 1));
    } else if (b.shape == Shape::Cylinder || b.shape == Shape::Cone) {
        for (int i = 0; i < n; ++i) {
            float a = 2 * pi * i / n, c = 2 * pi * (i + 1) / n;
            QVector3D na(std::cos(a), std::sin(a), 0), nc(std::cos(c), std::sin(c), 0);
            QVector3D ba = na * r + QVector3D(0, 0, -h / 2), bc = nc * r + QVector3D(0, 0, -h / 2);
            tri(mesh, {{0, 0, -h / 2}, {0, 0, -1}}, {ba, {0, 0, -1}}, {bc, {0, 0, -1}});
            if (b.shape == Shape::Cylinder) {
                QVector3D ta = ba + QVector3D(0, 0, h), tc = bc + QVector3D(0, 0, h);
                quad(mesh, {ba, na}, {bc, nc}, {tc, nc}, {ta, na});
                tri(mesh, {{0, 0, h / 2}, {0, 0, 1}}, {ta, {0, 0, 1}}, {tc, {0, 0, 1}});
            } else {
                na = QVector3D(na.x(), na.y(), r / h).normalized();
                nc = QVector3D(nc.x(), nc.y(), r / h).normalized();
                tri(mesh, {ba, na}, {bc, nc}, {{0, 0, h / 2}, (na + nc).normalized()});
            }
        }
    } else if (b.shape == Shape::Torus) {
        auto point = [&](int row, int col) {
            float u = 2 * pi * row / n, v = 2 * pi * col / rings;
            QVector3D normal(std::cos(u) * std::cos(v), std::sin(u) * std::cos(v), std::sin(v));
            return Vertex{{(r + tube * std::cos(v)) * std::cos(u), (r + tube * std::cos(v)) * std::sin(u),
                           tube * std::sin(v)},
                          normal};
        };
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < rings; ++i)
                quad(mesh, point(j, i), point(j + 1, i), point(j + 1, i + 1), point(j, i + 1));
    }
    return mesh;
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
