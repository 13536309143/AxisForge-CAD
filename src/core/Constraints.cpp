#include "Constraints.h"
#include <QHash>
#include <QStringList>
#include <QtMath>
#include <cmath>
namespace af {
Residual residual(const Scene &s, const Mate &m) {
    Residual r;
    auto a = s.find(m.source), b = s.find(m.target);
    if (!a || !b) {
        r.linear = 1e20;
        return r;
    }
    const auto x = a->axis(m.sourceAxis), y = b->axis(m.targetAxis), delta = b->position - a->position;
    if (m.type == MateType::Parallel || m.type == MateType::Coaxial)
        r.angular = QVector3D::crossProduct(x, y).length();
    if (m.type == MateType::Perpendicular)
        r.angular = std::abs(QVector3D::dotProduct(x, y));
    if (m.type == MateType::Coaxial)
        r.linear = (delta - QVector3D::dotProduct(delta, x) * x).length();
    if (m.type == MateType::Distance)
        r.linear = std::abs(delta.length() - m.distance);
    if (m.type == MateType::Coincident)
        r.linear = delta.length();
    if (m.type == MateType::AxialOffset)
        r.linear = std::abs(QVector3D::dotProduct(delta, x) - m.offset);
    if (m.type == MateType::Angle)
        r.angular = std::abs(
            std::atan2(double(QVector3D::crossProduct(x, y).length()), double(QVector3D::dotProduct(x, y))) -
            qDegreesToRadians(m.angle));
    return r;
}
bool solve(Scene &scene, QString *error) {
    auto fail = [&](QString s) {
        if (error)
            *error = s;
        return false;
    };
    if (!scene.validate(error))
        return false;
    Scene work = scene;
    QHash<QString, int> indegree;
    QHash<QString, QVector<QString>> next;
    for (const auto &b : work.bodies)
        indegree[b.id] = 0;
    for (const auto &m : work.mates)
        if (m.enabled) {
            ++indegree[m.target];
            next[m.source].push_back(m.target);
        }
    QVector<QString> order;
    for (const auto &b : work.bodies)
        if (indegree[b.id] == 0)
            order.push_back(b.id);
    for (int i = 0; i < order.size(); ++i)
        for (const auto &id : next[order[i]])
            if (--indegree[id] == 0)
                order.push_back(id);
    if (order.size() != work.bodies.size())
        return fail(QStringLiteral("约束存在有向循环。请移除循环引用，采用基准零件 → 从动零件的层级。"));
    for (const auto &id : order) {
        auto *b = work.find(id);
        if (b->fixed)
            continue;
        for (int pass = 0; pass < 64; ++pass) {
            for (const auto &m : work.mates) {
                if (!m.enabled || m.target != id)
                    continue;
                const Body *a = work.find(m.source);
                const auto sourceAxis = a->axis(m.sourceAxis);
                auto targetAxis = b->axis(m.targetAxis);
                if (m.type == MateType::Parallel || m.type == MateType::Coaxial) {
                    // Axes are unoriented: retain an already antiparallel orientation.
                    auto desired =
                        QVector3D::dotProduct(sourceAxis, targetAxis) < 0 ? -sourceAxis : sourceAxis;
                    b->rotation = (QQuaternion::rotationTo(targetAxis, desired) * b->rotation).normalized();
                } else if (m.type == MateType::Perpendicular) {
                    auto desired = targetAxis - QVector3D::dotProduct(targetAxis, sourceAxis) * sourceAxis;
                    if (desired.length() < 1e-5f) {
                        auto helper =
                            std::abs(sourceAxis.x()) < 0.8f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
                        desired = QVector3D::crossProduct(sourceAxis, helper);
                    }
                    b->rotation = (QQuaternion::rotationTo(targetAxis, desired.normalized()) * b->rotation)
                                      .normalized();
                }
                if (m.type == MateType::Angle) {
                    auto radial = targetAxis - QVector3D::dotProduct(targetAxis, sourceAxis) * sourceAxis;
                    if (radial.lengthSquared() < 1e-10f) {
                        const auto helper =
                            std::abs(sourceAxis.x()) < .8f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
                        radial = QVector3D::crossProduct(sourceAxis, helper);
                    }
                    const double theta = qDegreesToRadians(m.angle);
                    const auto desired =
                        sourceAxis * float(std::cos(theta)) + radial.normalized() * float(std::sin(theta));
                    b->rotation = (QQuaternion::rotationTo(targetAxis, desired) * b->rotation).normalized();
                }
                auto delta = b->position - a->position;
                if (m.type == MateType::Coincident)
                    b->position = a->position;
                if (m.type == MateType::AxialOffset)
                    b->position += sourceAxis * float(m.offset - QVector3D::dotProduct(delta, sourceAxis));
                if (m.type == MateType::Coaxial)
                    b->position = a->position + QVector3D::dotProduct(delta, sourceAxis) * sourceAxis;
                if (m.type == MateType::Distance) {
                    if (delta.length() < 1e-6f)
                        delta = sourceAxis;
                    b->position = a->position + delta.normalized() * float(m.distance);
                }
            }
            bool satisfied = true;
            for (const auto &m : work.mates)
                if (m.enabled && m.target == id && !residual(work, m).satisfied())
                    satisfied = false;
            if (satisfied)
                break;
        }
    }
    for (const auto &m : work.mates)
        if (m.enabled && !residual(work, m).satisfied())
            return fail(QStringLiteral("约束无法同时满足，可能过约束或从动模型已固定。修改已回滚。"));
    if (!work.validate(error))
        return false;
    scene = work;
    return true;
}
} // namespace af
