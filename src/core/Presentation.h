#pragma once
#include "Geometry.h"
namespace af {
struct GuideLine {
    QVector3D a, b;
    QColor color;
    bool dashed = false;
};
struct GuideLabel {
    QVector3D position;
    QString text;
    QColor color;
};
struct MateGuide {
    QString title, detail, source, target;
    QVector<GuideLine> lines;
    QVector<GuideLabel> labels;
    bool valid = false, visible = false, satisfied = false, enabled = false;
};
MateGuide mateGuide(const Scene &scene, const Mate &mate);
const Mate *findMate(const Scene &scene, const QString &id);
Bounds visibleWorldBounds(const Scene &scene);
struct GridPlan {
    double minX = -100, maxX = 100, minY = -100, maxY = 100, z = 0, step = 10, axisLength = 40;
    bool operator==(const GridPlan &other) const;
};
GridPlan adaptiveGrid(const Bounds &bounds, const QVector3D &cameraTarget, double cameraDistance);
} // namespace af
