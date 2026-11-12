#include "Presentation.h"
#include "Constraints.h"
#include "Kernel.h"
#include <QtMath>
#include <algorithm>
#include <cmath>
namespace af {
namespace {
const QColor sourceColor("#55d8ef"), targetColor("#ffb866"), measureColor("#f3e9a7");
QVector3D orthogonal(const QVector3D &axis) {
    auto helper = std::abs(axis.x()) < .8f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
    return QVector3D::crossProduct(axis, helper).normalized();
}
double nice(double size) {
    size = qMax(.001, size);
    const double scale = std::pow(10.0, std::floor(std::log10(size))), x = size / scale;
    return (x <= 1 ? 1 : x <= 2 ? 2 : x <= 5 ? 5 : 10) * scale;
}
} // namespace
const Mate *findMate(const Scene &scene, const QString &id) {
    for (const auto &m : scene.mates)
        if (m.id == id)
            return &m;
    return nullptr;
}
Bounds visibleWorldBounds(const Scene &scene) {
    Bounds result;
    for (const auto &body : scene.bodies)
        if (body.visible) {
            const auto bounds = solidData(body)->bounds;
            const auto matrix = body.matrix();
            for (int i = 0; i < 8; ++i)
                result.include(matrix.map({i & 1 ? bounds.max.x() : bounds.min.x(),
                                           i & 2 ? bounds.max.y() : bounds.min.y(),
                                           i & 4 ? bounds.max.z() : bounds.min.z()}));
        }
    return result;
}
bool GridPlan::operator==(const GridPlan &o) const {
    return minX == o.minX && maxX == o.maxX && minY == o.minY && maxY == o.maxY && z == o.z &&
           step == o.step && axisLength == o.axisLength;
}
GridPlan adaptiveGrid(const Bounds &bounds, const QVector3D &target, double distance) {
    const double radius = qBound(20.0, distance * .65, 10000000.0);
    double minX = target.x() - radius, maxX = target.x() + radius, minY = target.y() - radius,
           maxY = target.y() + radius;
    if (bounds.valid) {
        const auto size = bounds.max - bounds.min;
        double margin = qMax(10.0, double(qMax(size.x(), size.y())) * .15);
        minX = qMin(minX, bounds.min.x() - margin);
        maxX = qMax(maxX, bounds.max.x() + margin);
        minY = qMin(minY, bounds.min.y() - margin);
        maxY = qMax(maxY, bounds.max.y() + margin);
    }
    GridPlan p;
    p.step = nice(qMax(maxX - minX, maxY - minY) / 40);
    p.minX = std::floor(minX / p.step) * p.step;
    p.maxX = std::ceil(maxX / p.step) * p.step;
    p.minY = std::floor(minY / p.step) * p.step;
    p.maxY = std::ceil(maxY / p.step) * p.step;
    p.z = (bounds.valid ? qMin(0.0, double(bounds.min.z())) : 0.0) - p.step * .001;
    p.axisLength = nice(qMax(maxX - minX, maxY - minY) * .12);
    return p;
}
MateGuide mateGuide(const Scene &scene, const Mate &m) {
    MateGuide g;
    g.source = m.source;
    g.target = m.target;
    g.enabled = m.enabled;
    auto a = scene.find(m.source), b = scene.find(m.target);
    if (!a || !b || a == b)
        return g;
    g.valid = true;
    g.visible = a->visible && b->visible;
    g.satisfied = residual(scene, m).satisfied();
    const auto u = a->axis(m.sourceAxis), v = b->axis(m.targetAxis), delta = b->position - a->position;
    const auto originA = a->position, originB = b->position, mid = (originA + originB) * .5f;
    float length = qMax(8.0f, qMax(solidData(*a)->bounds.radius(), solidData(*b)->bounds.radius()) * 1.4f);
    g.title = mateName(m.type) +
              (m.enabled ? (g.satisfied ? QStringLiteral(" · 满足") : QStringLiteral(" · 未满足"))
                         : QStringLiteral(" · 已停用"));
    g.detail =
        QStringLiteral("A: %1 [%2] → B: %3 [%4]")
            .arg(a->name, QString("XYZ").mid(m.sourceAxis, 1), b->name, QString("XYZ").mid(m.targetAxis, 1));
    if (!g.visible) {
        g.detail += QStringLiteral(" · 参与对象被隐藏，标记不绘制");
        return g;
    }
    auto line = [&](QVector3D x, QVector3D y, QColor c, bool dashed = false) {
        g.lines << GuideLine{x, y, m.enabled ? c : QColor("#a1a8b4"), dashed || !m.enabled};
    };
    auto label = [&](QVector3D p, QString text, QColor c = measureColor) {
        g.labels << GuideLabel{p, text, m.enabled ? c : QColor("#a1a8b4")};
    };
    auto axis = [&](QVector3D p, QVector3D d, QColor c, QString text, bool dashed = false) {
        line(p - d * length, p + d * length, c, dashed);
        label(p + d * length, text, c);
    };
    auto cross = [&](QVector3D p, QColor c) {
        float s = length * .07f;
        for (auto d : {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)})
            line(p - s * d, p + s * d, c);
    };
    auto dimension = [&](QVector3D p, QVector3D q, const QString &text) {
        line(p, q, measureColor);
        auto d = (q - p).normalized();
        if (d.isNull())
            d = u;
        auto side = orthogonal(d) * length * .065f;
        line(p - side, p + side, measureColor);
        line(q - side, q + side, measureColor);
        const auto arrow = d * length * .10f;
        line(p, p + arrow + side * .5f, measureColor);
        line(p, p + arrow - side * .5f, measureColor);
        line(q, q - arrow + side * .5f, measureColor);
        line(q, q - arrow - side * .5f, measureColor);
        label((p + q) * .5f + side * 2, text);
    };
    cross(originA, sourceColor);
    cross(originB, targetColor);
    switch (m.type) {
    case MateType::Coaxial:
        length = qMax(length, delta.length() * .65f + length * .25f);
        axis(originA, u, sourceColor, "A " + QString("XYZ").mid(m.sourceAxis, 1));
        axis(originB, v, targetColor, "B " + QString("XYZ").mid(m.targetAxis, 1), true);
        label(mid, QStringLiteral("同轴 · 共用轴线"));
        break;
    case MateType::Parallel:
        axis(originA, u, sourceColor, "A");
        axis(originB, v, targetColor, "B");
        for (auto p : {originA, originB}) {
            auto side = orthogonal(u) * length * .08f;
            line(p - side - u * length * .06f, p + side + u * length * .06f, measureColor);
            line(p - side + u * length * .06f, p + side + u * length * .18f, measureColor);
        }
        label(mid, QStringLiteral("∥ 平行轴（含反平行）"));
        break;
    case MateType::Distance:
        dimension(originA, originB,
                  QStringLiteral("中心距 %1 mm / 设定 %2 mm")
                      .arg(delta.length(), 0, 'f', 3)
                      .arg(m.distance, 0, 'f', 3));
        break;
    case MateType::Coincident:
        line(originA, originB, measureColor, true);
        label(mid, QStringLiteral("原点重合 · 偏差 %1 mm").arg(delta.length(), 0, 'f', 3));
        break;
    case MateType::AxialOffset: {
        axis(originA, u, sourceColor, "A");
        float current = QVector3D::dotProduct(delta, u);
        auto foot = originA + u * current;
        line(foot, originB, targetColor, true);
        dimension(originA, foot,
                  QStringLiteral("轴向 %1 mm / 设定 %2 mm").arg(current, 0, 'f', 3).arg(m.offset, 0, 'f', 3));
        break;
    }
    case MateType::Perpendicular:
    case MateType::Angle: {
        axis(originA, u, sourceColor, "A");
        axis(originB, v, targetColor, "B");
        // The two directions are translated to a shared display origin; leaders expose that translation.
        auto anchor = mid + QVector3D(0, 0, length * .85f);
        float r = length * .55f;
        auto side = v - QVector3D::dotProduct(u, v) * u;
        if (side.lengthSquared() < 1e-10f)
            side = orthogonal(u);
        side.normalize();
        double theta =
            std::atan2(double(QVector3D::crossProduct(u, v).length()), double(QVector3D::dotProduct(u, v)));
        line(originA, anchor, sourceColor, true);
        line(originB, anchor, targetColor, true);
        line(anchor, anchor + u * r * 1.3f, sourceColor);
        line(anchor, anchor + v * r * 1.3f, targetColor);
        for (int i = 0; i < 32; ++i) {
            auto point = [&](double t) {
                return anchor + (u * float(std::cos(t)) + side * float(std::sin(t))) * r;
            };
            line(point(theta * i / 32), point(theta * (i + 1) / 32), measureColor);
        }
        auto half = (u * float(std::cos(theta / 2)) + side * float(std::sin(theta / 2))).normalized();
        label(anchor + half * r * 1.2f,
              QStringLiteral("%1° / 设定 %2°")
                  .arg(qRadiansToDegrees(theta), 0, 'f', 2)
                  .arg(m.type == MateType::Perpendicular ? 90 : m.angle, 0, 'f', 2));
        label(anchor - QVector3D(0, 0, r * .15f), QStringLiteral("方向示意（平移至共同顶点）"));
        break;
    }
    }
    return g;
}
} // namespace af
