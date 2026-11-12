#include "Kernel.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <cmath>
#include <gp_Quaternion.hxx>
#include <gp_Trsf.hxx>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace af {
namespace {
constexpr double pi = 3.14159265358979323846;
gp_Trsf placement(const Body &b) {
    gp_Trsf t;
    const auto q = b.rotation;
    t.SetRotation(gp_Quaternion(q.x(), q.y(), q.z(), q.scalar()));
    t.SetTranslationPart(gp_Vec(b.position.x(), b.position.y(), b.position.z()));
    return t;
}
int countShapes(const TopoDS_Shape &s, TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(s, type, map);
    return map.Extent();
}
QString failure(const Standard_Failure &e) {
    return QString::fromUtf8(e.GetMessageString() ? e.GetMessageString() : "OCCT error");
}
} // namespace
QString kernelVersion() {
    return QStringLiteral(OCC_VERSION_COMPLETE);
}
void initializeKernel() {
    static std::once_flag once;
    std::call_once(once, [] {
        QDir directory(QCoreApplication::applicationDirPath() + "/occt-resources");
        if (!directory.exists())
            return;
        const std::pair<const char *, const char *> entries[] = {
            {"CSF_XSMessage", "XSMessage"},          {"CSF_SHMessage", "SHMessage"},
            {"CSF_STEPDefaults", "XSTEPResource"},   {"CSF_PluginDefaults", "StdResource"},
            {"CSF_StandardDefaults", "StdResource"}, {"CSF_XCAFDefaults", "StdResource"}};
        for (const auto &entry : entries)
            if (qEnvironmentVariableIsEmpty(entry.first))
                qputenv(entry.first, QFile::encodeName(directory.filePath(entry.second)));
    });
}
std::shared_ptr<const SolidData> createSolid(const TopoDS_Shape &input, int segments) {
    if (input.IsNull())
        throw std::runtime_error("实体为空。");
    // Copies isolate triangulation updates from shared shapes held by undo snapshots.
    TopoDS_Shape shape = BRepBuilderAPI_Copy(input, Standard_False, Standard_False).Shape();
    int solids = countShapes(shape, TopAbs_SOLID), faces = countShapes(shape, TopAbs_FACE);
    if (!solids || faces > 20000 || !BRepCheck_Analyzer(shape).IsValid())
        throw std::runtime_error("需要有效的闭合 OCCT 实体，面数不得超过20000。");
    // A compound may contain multiple solids, but never loose shells/faces/edges.
    TopTools_IndexedMapOfShape allFaces, solidFaces;
    TopExp::MapShapes(shape, TopAbs_FACE, allFaces);
    for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next())
        TopExp::MapShapes(e.Current(), TopAbs_FACE, solidFaces);
    if (allFaces.Extent() != solidFaces.Extent())
        throw std::runtime_error("输入包含未封闭曲面，不能作为实体使用。");
    for (auto kind : {TopAbs_EDGE, TopAbs_VERTEX}) {
        TopTools_IndexedMapOfShape all, inside;
        TopExp::MapShapes(shape, kind, all);
        for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next())
            TopExp::MapShapes(e.Current(), kind, inside);
        if (all.Extent() != inside.Extent())
            throw std::runtime_error("复合体包含不属于实体的线框或孤立点。");
    }
    auto data = std::make_shared<SolidData>();
    data->shape = shape;
    data->solids = solids;
    data->faces = faces;
    data->edges = countShapes(shape, TopAbs_EDGE);
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    data->volume = props.Mass();
    BRepGProp::SurfaceProperties(shape, props);
    data->area = props.Mass();
    if (!std::isfinite(data->volume) || data->volume <= 1e-10 || !std::isfinite(data->area))
        throw std::runtime_error("实体体积无效或运算结果没有体积。");
    const double angle = 2 * pi / qBound(8, segments, 128);
    BRepMesh_IncrementalMesh mesher(shape, 0.1, Standard_True, angle, Standard_False);
    if (!mesher.IsDone())
        throw std::runtime_error("OCCT 网格生成失败。");
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        auto face = TopoDS::Face(explorer.Current());
        TopLoc_Location location;
        Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull())
            throw std::runtime_error("实体的某个面无法生成显示网格。");
        BRepAdaptor_Surface surface(face, Standard_False);
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
            int ids[3];
            triangulation->Triangle(i).Get(ids[0], ids[1], ids[2]);
            if (reversed)
                std::swap(ids[1], ids[2]);
            Vertex vertices[3];
            for (int j = 0; j < 3; ++j) {
                auto p = triangulation->Node(ids[j]).Transformed(location.Transformation());
                gp_Vec n;
                if (triangulation->HasUVNodes()) {
                    auto uv = triangulation->UVNode(ids[j]);
                    gp_Pnt point;
                    gp_Vec du, dv;
                    surface.D1(uv.X(), uv.Y(), point, du, dv);
                    n = du.Crossed(dv);
                    if (n.SquareMagnitude() > 1e-20)
                        n.Normalize();
                }
                if (reversed)
                    n.Reverse();
                vertices[j] = {{float(p.X()), float(p.Y()), float(p.Z())},
                               {float(n.X()), float(n.Y()), float(n.Z())}};
            }
            auto cross = QVector3D::crossProduct(vertices[1].position - vertices[0].position,
                                                 vertices[2].position - vertices[0].position);
            if (cross.lengthSquared() < 1e-12f)
                continue;
            for (auto &v : vertices) {
                if (v.normal.lengthSquared() < 1e-12f)
                    v.normal = cross.normalized();
                else
                    v.normal.normalize();
                data->mesh.vertices << v;
            }
            if (data->mesh.vertices.size() > 3000000)
                throw std::runtime_error("显示网格超过100万个三角形限制。");
        }
    }
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (int edgeIndex = 1; edgeIndex <= edges.Extent(); ++edgeIndex) {
        const auto edge = TopoDS::Edge(edges(edgeIndex));
        if (BRep_Tool::Degenerated(edge))
            continue;
        BRepAdaptor_Curve curve(edge);
        const double first = curve.FirstParameter(), last = curve.LastParameter();
        if (!std::isfinite(first) || !std::isfinite(last))
            continue;
        const int points = curve.GetType() == GeomAbs_Line ? 2 : qBound(8, segments, 128) + 1;
        for (int i = 0; i < points - 1; ++i) {
            const auto a = curve.Value(first + (last - first) * i / (points - 1)),
                       b = curve.Value(first + (last - first) * (i + 1) / (points - 1));
            data->edgeVertices << Vertex{{float(a.X()), float(a.Y()), float(a.Z())}, {0, 0, 1}}
                               << Vertex{{float(b.X()), float(b.Y()), float(b.Z())}, {0, 0, 1}};
        }
    }
    if (data->mesh.vertices.isEmpty())
        throw std::runtime_error("实体没有可显示的三角形。");
    std::ostringstream storage;
    BRepTools::Write(shape, storage, Standard_False, Standard_False, TopTools_FormatVersion_VERSION_1);
    const auto raw = storage.str();
    if (raw.size() > 16 * 1024 * 1024)
        throw std::runtime_error("单个实体的 B-Rep 数据超过16 MiB限制。");
    data->brep = qCompress(QByteArray(raw.data(), int(raw.size())), 6).toBase64();
    return data;
}
std::shared_ptr<const SolidData> solidData(const Body &b) {
    initializeKernel();
    if (b.shape == Shape::Solid) {
        if (!b.solid)
            throw std::runtime_error("实体数据缺失。");
        return b.solid;
    }
    const auto &p = b.params;
    QString key = QString("%1/%2/%3/%4/%5/%6/%7")
                      .arg(int(b.shape))
                      .arg(p.width, 0, 'g', 17)
                      .arg(p.depth, 0, 'g', 17)
                      .arg(p.height, 0, 'g', 17)
                      .arg(p.radius, 0, 'g', 17)
                      .arg(p.tubeRadius, 0, 'g', 17)
                      .arg(p.segments);
    if (b.generated && b.geometryKey == key)
        return b.generated;
    TopoDS_Shape shape;
    switch (b.shape) {
    case Shape::Box:
        shape =
            BRepPrimAPI_MakeBox(gp_Pnt(-p.width / 2, -p.depth / 2, -p.height / 2), p.width, p.depth, p.height)
                .Shape();
        break;
    case Shape::Sphere:
        shape = BRepPrimAPI_MakeSphere(p.radius).Shape();
        break;
    case Shape::Cylinder:
        shape =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -p.height / 2), gp_Dir(0, 0, 1)), p.radius, p.height)
                .Shape();
        break;
    case Shape::Cone:
        shape =
            BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0, 0, -p.height / 2), gp_Dir(0, 0, 1)), p.radius, 0, p.height)
                .Shape();
        break;
    case Shape::Torus:
        shape = BRepPrimAPI_MakeTorus(p.radius, p.tubeRadius).Shape();
        break;
    case Shape::Solid:
        break;
    }
    b.generated = createSolid(shape, p.segments);
    b.geometryKey = key;
    return b.generated;
}
TopoDS_Shape worldShape(const Body &body) {
    return BRepBuilderAPI_Transform(solidData(body)->shape, placement(body), Standard_True).Shape();
}
bool booleanBody(const Body &a, const Body &b, BooleanOp op, Body &result, QString *error) {
    try {
        if (a.id == b.id)
            throw std::runtime_error("布尔操作需要两个不同的模型。");
        auto left = worldShape(a), right = worldShape(b);
        TopoDS_Shape output;
        auto perform = [&](auto &algorithm) {
            algorithm.SetNonDestructive(Standard_True);
            algorithm.SetRunParallel(Standard_False);
            algorithm.Build();
            if (!algorithm.IsDone() || algorithm.HasErrors())
                throw std::runtime_error("OCCT 布尔运算失败，原模型未修改。");
            return algorithm.Shape();
        };
        if (op == BooleanOp::Fuse) {
            BRepAlgoAPI_Fuse algorithm(left, right);
            output = perform(algorithm);
        }
        if (op == BooleanOp::Cut) {
            BRepAlgoAPI_Cut algorithm(left, right);
            output = perform(algorithm);
        }
        if (op == BooleanOp::Common) {
            BRepAlgoAPI_Common algorithm(left, right);
            output = perform(algorithm);
        }
        Body body = makeBody(Shape::Box);
        body.shape = Shape::Solid;
        body.name = (op == BooleanOp::Fuse  ? QStringLiteral("并集")
                     : op == BooleanOp::Cut ? QStringLiteral("差集 A−B")
                                            : QStringLiteral("交集")) +
                    " · " + a.name;
        // Result is centered in A's local frame, so moving it behaves naturally.
        const gp_Trsf inverse = placement(a).Inverted();
        body.solid = createSolid(BRepBuilderAPI_Transform(output, inverse, Standard_True).Shape(),
                                 qMax(a.params.segments, b.params.segments));
        body.position = a.position;
        body.rotation = a.rotation;
        body.color = a.color;
        body.params.segments = qMax(a.params.segments, b.params.segments);
        result = body;
        return true;
    } catch (const Standard_Failure &e) {
        if (error)
            *error = failure(e);
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return false;
}
QByteArray encodeBrep(const Body &body) {
    return solidData(body)->brep;
}
bool decodeBrep(const QByteArray &bytes, Body &body, QString *error) {
    try {
        auto decoded = QByteArray::fromBase64Encoding(bytes, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.size() < 4)
            throw std::runtime_error("B-Rep 编码无效。");
        const auto &packed = decoded.decoded;
        quint32 size = 0;
        for (int i = 0; i < 4; ++i)
            size = (size << 8) | quint8(packed[i]);
        if (size == 0 || size > 16 * 1024 * 1024)
            throw std::runtime_error("单个 B-Rep 解压后不得超过16 MiB。");
        QByteArray raw = qUncompress(packed);
        if (raw.size() != int(size))
            throw std::runtime_error("B-Rep 数据损坏。");
        std::istringstream stream(std::string(raw.constData(), size_t(raw.size())));
        TopoDS_Shape shape;
        BRep_Builder builder;
        BRepTools::Read(shape, stream, builder);
        body.solid = createSolid(shape, body.params.segments);
        body.shape = Shape::Solid;
        return true;
    } catch (const Standard_Failure &e) {
        if (error)
            *error = failure(e);
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return false;
}
} // namespace af
