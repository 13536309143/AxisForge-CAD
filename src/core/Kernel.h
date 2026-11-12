#pragma once
#include "Geometry.h"
#include <TopoDS_Shape.hxx>
namespace af {
enum class BooleanOp { Fuse, Cut, Common };
struct SolidData {
    TopoDS_Shape shape;
    Mesh mesh;
    Bounds bounds;
    QVector<Vertex> edgeVertices;
    QByteArray brep;
    double volume = 0, area = 0;
    int solids = 0, faces = 0, edges = 0;
};
std::shared_ptr<const SolidData> solidData(const Body &body);
std::shared_ptr<const SolidData> createSolid(const TopoDS_Shape &shape, int segments = 48);
TopoDS_Shape worldShape(const Body &body);
bool booleanBody(const Body &a, const Body &b, BooleanOp op, Body &result, QString *error = nullptr);
QByteArray encodeBrep(const Body &body);
bool decodeBrep(const QByteArray &bytes, Body &body, QString *error = nullptr);
bool importStep(const QString &path, QVector<Body> &bodies, QString *error = nullptr);
bool exportStep(const QString &path, const Scene &scene, QString *error = nullptr);
QString kernelVersion();
void initializeKernel();
} // namespace af
