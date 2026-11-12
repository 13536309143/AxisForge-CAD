#include "MeshIO.h"
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace af {
namespace {
constexpr int MaxBytes = 64 * 1024 * 1024, MaxVertices = 3000000;
[[noreturn]] void bad(const QString &s) {
    throw std::runtime_error(s.toUtf8().constData());
}
void require(bool ok, const QString &message) {
    if (!ok)
        bad(message);
}
double number(const QJsonValue &v) {
    require(v.isDouble() && std::isfinite(v.toDouble()), QStringLiteral("GLB 数值缺失或非有限。"));
    return v.toDouble();
}
int integer(const QJsonValue &v, int fallback = -1) {
    if (v.isUndefined())
        return fallback;
    double n = number(v);
    require(n >= 0 && n <= MaxBytes && n == std::floor(n), QStringLiteral("GLB 整数字段无效。"));
    return int(n);
}
QJsonObject objectAt(const QJsonArray &a, int i) {
    require(i >= 0 && i < a.size() && a[i].isObject(), QStringLiteral("GLB 引用索引越界。"));
    return a[i].toObject();
}
QJsonArray array(const QJsonObject &o, const char *key) {
    require(o[key].isArray(), QStringLiteral("GLB 数组缺失：") + key);
    return o[key].toArray();
}
QByteArray readFile(const QString &path) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), QStringLiteral("无法读取文件：") + path);
    require(f.size() > 0 && f.size() <= MaxBytes, QStringLiteral("文件为空或超过 64 MiB。"));
    auto bytes = f.read(MaxBytes + 1LL);
    require(bytes.size() <= MaxBytes && f.atEnd() && f.error() == QFile::NoError,
            QStringLiteral("文件读取失败或超过限制。"));
    return bytes;
}
void writeFile(const QString &path, const QByteArray &bytes) {
    require(bytes.size() <= MaxBytes, QStringLiteral("输出超过 64 MiB。"));
    QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit(),
            QStringLiteral("文件原子写入失败：") + path);
}
void utf8(QTextStream &stream) {
    stream.setLocale(QLocale::c());
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#endif
}
void warning(QStringList &list, const QString &s) {
    if (!list.contains(s))
        list << s;
}
bool finite(const QVector3D &v) {
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z()) &&
           std::abs(v.x()) <= 1000000 && std::abs(v.y()) <= 1000000 && std::abs(v.z()) <= 1000000;
}
void triangle(Mesh &mesh, Vertex a, Vertex b, Vertex c) {
    require(mesh.vertices.size() <= MaxVertices - 3, QStringLiteral("网格超过 100 万三角形限制。"));
    require(finite(a.position) && finite(b.position) && finite(c.position),
            QStringLiteral("网格坐标非有限或超过 ±1000000 mm。"));
    auto n = QVector3D::crossProduct(b.position - a.position, c.position - a.position);
    require(std::isfinite(n.lengthSquared()) && n.lengthSquared() > 1e-16f,
            QStringLiteral("网格包含退化三角形。"));
    n.normalize();
    for (auto *v : {&a, &b, &c}) {
        require(std::isfinite(v->normal.x()) && std::isfinite(v->normal.y()) &&
                    std::isfinite(v->normal.z()) && std::isfinite(v->normal.lengthSquared()),
                QStringLiteral("网格法线非有限。"));
        v->normal = v->normal.lengthSquared() > 1e-16f ? v->normal.normalized() : n;
    }
    mesh.vertices << a << b << c;
}
void appendBody(QVector<Body> &out, const QString &name, const Mesh &mesh, const QColor &color, int &total) {
    if (mesh.vertices.isEmpty())
        return;
    total += mesh.vertices.size();
    require(total <= MaxVertices && out.size() < 300, QStringLiteral("导入总网格或对象数量超过限制。"));
    // Keep the final world position while keeping local coordinates near the model.
    Bounds bounds;
    for (const auto &v : mesh.vertices)
        bounds.include(v.position);
    const auto center = (bounds.min + bounds.max) * 0.5f;
    Mesh local = mesh;
    for (auto &v : local.vertices)
        v.position -= center;
    auto b = makeMeshBody(name.left(160), local, color);
    b.position = center;
    out << b;
}
void validScene(const Scene &s) {
    QString why;
    require(s.validate(&why), why);
}
void validateImported(const QVector<Body> &bodies) {
    require(!bodies.isEmpty(), QStringLiteral("文件中没有可导入的三角网格。"));
    Scene s;
    s.bodies = bodies;
    validScene(s);
}
quint32 u32(const QByteArray &b, int offset) {
    require(offset >= 0 && qint64(offset) + 4 <= b.size(), QStringLiteral("二进制数据截断。"));
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + offset));
}
void addU32(QByteArray &b, quint32 v) {
    char bytes[4];
    qToLittleEndian(v, reinterpret_cast<uchar *>(bytes));
    b.append(bytes, 4);
}
void addFloat(QByteArray &b, float v) {
    quint32 bits;
    std::memcpy(&bits, &v, 4);
    addU32(b, bits);
}
QVector3D fromGltf(const QVector3D &v) {
    return {v.x(), -v.z(), v.y()};
}
QVector3D toGltf(const QVector3D &v) {
    return {v.x(), v.z(), -v.y()};
}
double linear(double c) {
    return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
}
double srgb(double c) {
    return c <= .0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - .055;
}

// A checked accessor never reads beyond its own bufferView, even when the BIN chunk is larger.
struct Accessor {
    QByteArray bin;
    qint64 start = 0;
    int count = 0, components = 0, componentType = 0, bytes = 0, stride = 0;
    bool normalized = false;
    double at(int index, int component = 0) const {
        require(index >= 0 && index < count && component >= 0 && component < components,
                QStringLiteral("GLB accessor 索引越界。"));
        const uchar *p = reinterpret_cast<const uchar *>(bin.constData()) + start + qint64(index) * stride +
                         component * bytes;
        double v = 0;
        switch (componentType) {
        case 5120:
            v = qint8(*p);
            if (normalized)
                v = qMax(-1.0, v / 127.0);
            break;
        case 5121:
            v = *p;
            if (normalized)
                v /= 255.0;
            break;
        case 5122:
            v = qFromLittleEndian<qint16>(p);
            if (normalized)
                v = qMax(-1.0, v / 32767.0);
            break;
        case 5123:
            v = qFromLittleEndian<quint16>(p);
            if (normalized)
                v /= 65535.0;
            break;
        case 5125:
            v = qFromLittleEndian<quint32>(p);
            break;
        case 5126: {
            quint32 bits = qFromLittleEndian<quint32>(p);
            float f;
            std::memcpy(&f, &bits, 4);
            v = f;
            break;
        }
        }
        require(std::isfinite(v), QStringLiteral("GLB 顶点数据包含 NaN/Inf。"));
        return v;
    }
    QVector3D vec(int i) const {
        return {float(at(i, 0)), float(at(i, 1)), float(at(i, 2))};
    }
};
Accessor accessor(const QJsonObject &root, const QByteArray &bin, int index) {
    auto a = objectAt(array(root, "accessors"), index);
    require(!a.contains("sparse"),
            QStringLiteral("暂不支持 sparse accessor；请先在建模软件中展开为普通顶点缓冲。"));
    Accessor out;
    out.bin = bin;
    auto type = a["type"].toString();
    out.components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
    out.componentType = integer(a["componentType"]);
    out.bytes = out.componentType == 5120 || out.componentType == 5121   ? 1
                : out.componentType == 5122 || out.componentType == 5123 ? 2
                : out.componentType == 5125 || out.componentType == 5126 ? 4
                                                                         : 0;
    out.count = integer(a["count"]);
    require(out.components && out.bytes && out.count > 0 && out.count <= MaxVertices,
            QStringLiteral("GLB accessor 类型或数量无效。"));
    require(!a.contains("normalized") || a["normalized"].isBool(),
            QStringLiteral("normalized 必须为布尔值。"));
    out.normalized = a["normalized"].toBool();
    auto v = objectAt(array(root, "bufferViews"), integer(a["bufferView"]));
    require(integer(v["buffer"]) == 0, QStringLiteral("GLB 仅支持内嵌的第一个 BIN 缓冲。"));
    const qint64 offset = integer(v.value("byteOffset"), 0), length = integer(v["byteLength"]);
    const int inner = integer(a.value("byteOffset"), 0), packed = out.bytes * out.components;
    out.stride = integer(v.value("byteStride"), packed);
    require(length > 0 && offset + length <= bin.size() && out.stride >= packed && out.stride <= 252 &&
                out.stride % out.bytes == 0 && inner % out.bytes == 0 && offset % out.bytes == 0 &&
                qint64(inner) + qint64(out.count - 1) * out.stride + packed <= length,
            QStringLiteral("GLB bufferView/accessor 范围、对齐或步长无效。"));
    out.start = offset + inner;
    return out;
}
QVector<double> values(const QJsonValue &v, int n) {
    require(v.isArray() && v.toArray().size() == n, QStringLiteral("GLB 变换或颜色向量长度无效。"));
    QVector<double> out;
    for (auto x : v.toArray())
        out << number(x);
    return out;
}
QMatrix4x4 nodeMatrix(const QJsonObject &n) {
    QMatrix4x4 m;
    if (n.contains("matrix")) {
        require(!n.contains("translation") && !n.contains("rotation") && !n.contains("scale"),
                QStringLiteral("GLB matrix 与 TRS 不可混用。"));
        auto v = values(n["matrix"], 16);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                m(r, c) = float(v[c * 4 + r]);
        require(m(3, 0) == 0 && m(3, 1) == 0 && m(3, 2) == 0 && m(3, 3) == 1,
                QStringLiteral("GLB 节点必须使用仿射变换。"));
    } else {
        if (n.contains("translation")) {
            auto v = values(n["translation"], 3);
            m.translate(float(v[0]), float(v[1]), float(v[2]));
        }
        if (n.contains("rotation")) {
            auto v = values(n["rotation"], 4);
            QQuaternion q{float(v[3]), float(v[0]), float(v[1]), float(v[2])};
            require(std::abs(q.length() - 1) < .002, QStringLiteral("GLB 旋转未归一化。"));
            m.rotate(q);
        }
        if (n.contains("scale")) {
            auto v = values(n["scale"], 3);
            m.scale(float(v[0]), float(v[1]), float(v[2]));
        }
    }
    require(std::isfinite(m.determinant()) && std::abs(m.determinant()) > 1e-20f,
            QStringLiteral("GLB 变换退化。"));
    return m;
}
QVector3D transformNormal(const QMatrix3x3 &m, const QVector3D &n) {
    return {m(0, 0) * n.x() + m(0, 1) * n.y() + m(0, 2) * n.z(),
            m(1, 0) * n.x() + m(1, 1) * n.y() + m(1, 2) * n.z(),
            m(2, 0) * n.x() + m(2, 1) * n.y() + m(2, 2) * n.z()};
}
QVector<Body> readGlb(const QByteArray &bytes, QStringList &warnings) {
    require(bytes.size() >= 20 && u32(bytes, 0) == 0x46546c67 && u32(bytes, 4) == 2 &&
                u32(bytes, 8) == quint32(bytes.size()),
            QStringLiteral("GLB 文件头、版本或长度无效。"));
    QByteArray json, bin;
    bool first = true, gotBin = false;
    for (qint64 pos = 12; pos < bytes.size();) {
        require(pos + 8 <= bytes.size(), QStringLiteral("GLB chunk 头截断。"));
        quint32 size = u32(bytes, int(pos)), type = u32(bytes, int(pos + 4));
        pos += 8;
        require(size % 4 == 0 && pos + size <= bytes.size(), QStringLiteral("GLB chunk 长度或对齐无效。"));
        if (first) {
            require(type == 0x4e4f534a && size <= 16 * 1024 * 1024, QStringLiteral("GLB 首块必须为 JSON。"));
            json = bytes.mid(int(pos), int(size));
        } else if (type == 0x004e4942) {
            require(!gotBin, QStringLiteral("GLB 包含重复 BIN。"));
            bin = bytes.mid(int(pos), int(size));
            gotBin = true;
        } else
            require(type != 0x4e4f534a, QStringLiteral("GLB 包含重复 JSON。"));
        first = false;
        pos += size;
    }
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(json, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(), QStringLiteral("GLB JSON 无效。"));
    auto root = doc.object();
    require(root["asset"].toObject()["version"].toString() == "2.0", QStringLiteral("仅支持 glTF 2.0。"));
    require(root.value("extensionsRequired").isUndefined() ||
                (root.value("extensionsRequired").isArray() &&
                 root.value("extensionsRequired").toArray().isEmpty()),
            QStringLiteral("GLB 含必需扩展（如 Draco/Meshopt）；请导出无压缩标准 GLB。"));
    auto buffers = array(root, "buffers");
    require(buffers.size() == 1 && gotBin, QStringLiteral("GLB 需要一个内嵌 BIN 缓冲。"));
    auto buffer = objectAt(buffers, 0);
    int length = integer(buffer["byteLength"]);
    require(!buffer.contains("uri") && length > 0 && length <= bin.size() && bin.size() - length <= 3,
            QStringLiteral("GLB BIN 描述无效或使用外部缓冲。"));
    bin.truncate(length);
    if (!root["animations"].toArray().isEmpty())
        warning(warnings, QStringLiteral("动画未导入，使用节点静态变换。"));
    auto nodes = array(root, "nodes"), meshes = array(root, "meshes");
    require(nodes.size() <= 10000, QStringLiteral("GLB 节点过多。"));
    QJsonArray sceneRoots;
    if (root.contains("scenes") && !array(root, "scenes").isEmpty()) {
        auto scene = objectAt(array(root, "scenes"), integer(root.value("scene"), 0));
        sceneRoots = array(scene, "nodes");
    } else {
        require(!root.contains("scene"), QStringLiteral("GLB 默认场景不存在。"));
        QSet<int> children;
        for (int i = 0; i < nodes.size(); ++i) {
            const auto n = objectAt(nodes, i);
            if (n.contains("children"))
                for (auto child : array(n, "children")) {
                    int id = integer(child);
                    objectAt(nodes, id);
                    children.insert(id);
                }
        }
        for (int i = 0; i < nodes.size(); ++i)
            if (!children.contains(i))
                sceneRoots << i;
    }
    QVector<Body> out;
    int total = 0, visits = 0;
    QSet<int> active;
    std::function<void(int, const QMatrix4x4 &, int)> visit;
    visit = [&](int id, const QMatrix4x4 &parent, int depth) {
        require(depth <= 64 && ++visits <= 10000 && !active.contains(id),
                QStringLiteral("GLB 节点循环、层级或实例超限。"));
        auto node = objectAt(nodes, id);
        active.insert(id);
        require(!node.contains("skin"), QStringLiteral("暂不支持骨骼蒙皮；请先烘焙为静态网格。"));
        auto world = parent * nodeMatrix(node);
        auto normal = world.normalMatrix();
        bool reversed = world.determinant() < 0;
        if (node.contains("mesh")) {
            auto mesh = objectAt(meshes, integer(node["mesh"]));
            auto primitives = array(mesh, "primitives");
            require(primitives.size() > 0 && primitives.size() <= 300,
                    QStringLiteral("GLB primitive 数量无效。"));
            for (int k = 0; k < primitives.size(); ++k) {
                auto p = objectAt(primitives, k);
                auto attrs = p["attributes"].toObject();
                require(!p.contains("targets"), QStringLiteral("暂不支持 morph targets；请先烘焙网格。"));
                auto positions = accessor(root, bin, integer(attrs["POSITION"]));
                require(positions.components == 3 && positions.componentType == 5126 && !positions.normalized,
                        QStringLiteral("POSITION 必须为 float VEC3。"));
                bool hasNormals = attrs.contains("NORMAL"), indexed = p.contains("indices");
                Accessor normals, indices;
                if (hasNormals) {
                    normals = accessor(root, bin, integer(attrs["NORMAL"]));
                    require(normals.components == 3 && normals.componentType == 5126 &&
                                normals.count == positions.count,
                            QStringLiteral("NORMAL 格式或数量无效。"));
                }
                if (indexed) {
                    indices = accessor(root, bin, integer(p["indices"]));
                    require(indices.components == 1 && !indices.normalized &&
                                (indices.componentType == 5121 || indices.componentType == 5123 ||
                                 indices.componentType == 5125),
                            QStringLiteral("索引必须为无符号 SCALAR。"));
                }
                int count = indexed ? indices.count : positions.count, mode = integer(p.value("mode"), 4);
                require((mode == 4 || mode == 5 || mode == 6) && count >= 3 && (mode != 4 || count % 3 == 0),
                        QStringLiteral("仅支持三角形、三角带和三角扇。"));
                auto idx = [&](int i) {
                    double v = indexed ? indices.at(i) : i;
                    require(v < positions.count, QStringLiteral("GLB 三角形索引越界。"));
                    return int(v);
                };
                Mesh result;
                auto emitTriangle = [&](int a, int b, int c) {
                    a = idx(a);
                    b = idx(b);
                    c = idx(c);
                    if (a == b || b == c || c == a)
                        return; // strip connectors
                    if (reversed)
                        std::swap(b, c);
                    auto vertex = [&](int i) {
                        return Vertex{fromGltf(world.map(positions.vec(i))) * 1000.0f,
                                      hasNormals ? fromGltf(transformNormal(normal, normals.vec(i)))
                                                 : QVector3D()};
                    };
                    triangle(result, vertex(a), vertex(b), vertex(c));
                };
                if (mode == 4)
                    for (int i = 0; i < count; i += 3)
                        emitTriangle(i, i + 1, i + 2);
                if (mode == 5)
                    for (int i = 2; i < count; ++i)
                        emitTriangle(i % 2 ? i - 1 : i - 2, i % 2 ? i - 2 : i - 1, i);
                if (mode == 6)
                    for (int i = 2; i < count; ++i)
                        emitTriangle(0, i - 1, i);
                QColor color = Qt::white;
                if (p.contains("material")) {
                    auto mat = objectAt(root["materials"].toArray(), integer(p["material"]));
                    auto pbr = mat["pbrMetallicRoughness"].toObject();
                    if (pbr.contains("baseColorFactor")) {
                        auto v = values(pbr["baseColorFactor"], 4);
                        for (double c : v)
                            require(c >= 0 && c <= 1, QStringLiteral("GLB 材质颜色越界。"));
                        color = QColor::fromRgbF(srgb(v[0]), srgb(v[1]), srgb(v[2]));
                    }
                    if (pbr.contains("baseColorTexture") || mat.contains("normalTexture") ||
                        pbr.contains("metallicRoughnessTexture"))
                        warning(warnings, QStringLiteral("纹理未保留，网格使用材质基础颜色。"));
                    if (mat["alphaMode"].toString("OPAQUE") != "OPAQUE")
                        warning(warnings, QStringLiteral("透明材质按不透明基础颜色显示。"));
                }
                if (attrs.contains("COLOR_0"))
                    warning(warnings, QStringLiteral("顶点颜色未保留，使用对象单色。"));
                QString name = node["name"].toString(mesh["name"].toString("GLB 网格"));
                if (primitives.size() > 1)
                    name += QString(" [%1]").arg(k + 1);
                appendBody(out, name, result, color, total);
            }
        }
        if (node.contains("children"))
            for (auto child : array(node, "children"))
                visit(integer(child), world, depth + 1);
        active.remove(id);
    };
    for (auto n : sceneRoots)
        visit(integer(n), QMatrix4x4(), 0);
    validateImported(out);
    return out;
}

struct ObjCorner {
    int position = -1, normal = -1;
};
double real(const QString &s) {
    bool ok = false;
    double x = s.toDouble(&ok);
    require(ok && std::isfinite(x), QStringLiteral("OBJ/MTL 包含无效数值。"));
    return x;
}
int objIndex(const QString &s, int size) {
    bool ok = false;
    qlonglong n = s.toLongLong(&ok);
    require(ok && n != 0, QStringLiteral("OBJ 索引无效。"));
    n = n > 0 ? n - 1 : size + n;
    require(n >= 0 && n < size, QStringLiteral("OBJ 索引越界。"));
    return int(n);
}
QStringList words(const QString &s) {
    return s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
}
// Ear clipping handles concave simple OBJ polygons instead of filling their concavities with a fan.
void polygon(Mesh &out, QVector<ObjCorner> corners, const QVector<QVector3D> &positions,
             const QVector<QVector3D> &normals) {
    if (corners.size() > 3 && corners.first().position == corners.last().position)
        corners.removeLast();
    require(corners.size() >= 3 && corners.size() <= 1024, QStringLiteral("OBJ 单面需要 3～1024 个顶点。"));
    QVector3D normal;
    for (int i = 0; i < corners.size(); ++i) {
        auto a = positions[corners[i].position], b = positions[corners[(i + 1) % corners.size()].position];
        normal += QVector3D::crossProduct(a, b);
    }
    int axis = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(normal[i]) > std::abs(normal[axis]))
            axis = i;
    const int x = (axis + 1) % 3, y = (axis + 2) % 3;
    auto cross = [&](int a, int b, int c) {
        const auto &p = positions[corners[a].position], &q = positions[corners[b].position],
                   &r = positions[corners[c].position];
        return (double(q[x]) - p[x]) * (double(r[y]) - p[y]) - (double(q[y]) - p[y]) * (double(r[x]) - p[x]);
    };
    double area = 0;
    for (int i = 0; i < corners.size(); ++i) {
        auto a = positions[corners[i].position], b = positions[corners[(i + 1) % corners.size()].position];
        area += double(a[x]) * b[y] - double(b[x]) * a[y];
    }
    require(std::abs(area) > 1e-12, QStringLiteral("OBJ 面退化或无法确定绕序。"));
    double sign = area > 0 ? 1 : -1;
    QVector<int> remaining;
    for (int i = 0; i < corners.size(); ++i)
        remaining << i;
    auto emitTriangle = [&](int a, int b, int c) {
        auto vertex = [&](int i) {
            auto v = corners[i];
            return Vertex{positions[v.position], v.normal < 0 ? QVector3D() : normals[v.normal]};
        };
        triangle(out, vertex(a), vertex(b), vertex(c));
    };
    while (remaining.size() > 3) {
        bool clipped = false;
        for (int i = 0; i < remaining.size(); ++i) {
            int a = remaining[(i + remaining.size() - 1) % remaining.size()], b = remaining[i],
                c = remaining[(i + 1) % remaining.size()];
            if (sign * cross(a, b, c) <= 1e-12)
                continue;
            bool inside = false;
            for (int p : remaining)
                if (p != a && p != b && p != c && sign * cross(a, b, p) >= -1e-12 &&
                    sign * cross(b, c, p) >= -1e-12 && sign * cross(c, a, p) >= -1e-12) {
                    inside = true;
                    break;
                }
            if (inside)
                continue;
            emitTriangle(a, b, c);
            remaining.removeAt(i);
            clipped = true;
            break;
        }
        require(clipped, QStringLiteral("OBJ 多边形自交、退化或无法三角化。"));
    }
    emitTriangle(remaining[0], remaining[1], remaining[2]);
}
QVector<Body> readObj(const QString &path, const QByteArray &bytes, QStringList &warnings) {
    QVector<QVector3D> positions, normals;
    int texcoords = 0, total = 0;
    QVector<Body> result;
    Mesh mesh;
    QString name = QFileInfo(path).completeBaseName(), material;
    QHash<QString, QColor> colors;
    const QDir directory = QFileInfo(path).absoluteDir();
    qint64 materialBytes = 0;
    int materialFiles = 0;
    auto flush = [&] {
        appendBody(result, name, mesh, colors.value(material, QColor("#b8c9d8")), total);
        mesh.vertices.clear();
    };
    auto parseMtl = [&](const QString &file) {
        require(++materialFiles <= 32, QStringLiteral("MTL 文件数量超过限制。"));
        QFileInfo info(directory.filePath(file));
        require(!QDir::isAbsolutePath(file) && !file.contains("..") &&
                    info.absolutePath() == directory.absolutePath(),
                QStringLiteral("MTL 仅允许同目录相对文件名。"));
        if (!info.exists()) {
            warning(warnings, QStringLiteral("未找到 MTL，使用默认颜色：") + file);
            return;
        }
        require(QFileInfo(info.canonicalFilePath()).absolutePath() == directory.canonicalPath(),
                QStringLiteral("MTL 不允许链接到目录之外。"));
        materialBytes += info.size();
        require(materialBytes <= 8 * 1024 * 1024, QStringLiteral("MTL 文件总量超过限制。"));
        QByteArray raw = readFile(info.absoluteFilePath());
        QString current;
        for (auto line : QString::fromUtf8(raw).split('\n')) {
            line = line.section('#', 0, 0).trimmed();
            auto w = words(line);
            if (w.isEmpty())
                continue;
            if (w[0] == "newmtl")
                current = line.mid(6).trimmed();
            else if (w[0] == "Kd" && !current.isEmpty()) {
                require(w.size() >= 4, QStringLiteral("MTL Kd 缺少颜色。"));
                double r = real(w[1]), g = real(w[2]), b = real(w[3]);
                require(r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1,
                        QStringLiteral("MTL Kd 颜色越界。"));
                colors[current] = QColor::fromRgbF(r, g, b);
            } else if (w[0].startsWith("map_") || w[0] == "bump")
                warning(warnings, QStringLiteral("OBJ 纹理未保留，使用 MTL Kd 颜色。"));
            else if (w[0] == "d" || w[0] == "Tr")
                warning(warnings, QStringLiteral("OBJ 透明度按不透明处理。"));
        }
    };
    QString text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    text.replace("\\\r\n", " ");
    text.replace("\\\n", " ");
    const auto sourceLines = text.split('\n');
    // Resolve material libraries first: valid OBJ files can declare them after faces.
    for (auto line : sourceLines) {
        line = line.section('#', 0, 0).trimmed();
        if (!line.startsWith("mtllib ") && !line.startsWith("mtllib\t"))
            continue;
        const auto file = line.mid(6).trimmed();
        if (directory.exists(file))
            parseMtl(file);
        else
            for (const auto &f : words(file))
                parseMtl(f);
    }
    int lines = 0;
    for (auto line : sourceLines) {
        require(++lines <= 6000000, QStringLiteral("OBJ 行数超限。"));
        line = line.section('#', 0, 0).trimmed();
        auto w = words(line);
        if (w.isEmpty())
            continue;
        if (w[0] == "v" || w[0] == "vn") {
            require(w.size() >= 4, QStringLiteral("OBJ 顶点或法线分量不足。"));
            QVector3D v(float(real(w[1])), float(real(w[2])), float(real(w[3])));
            if (w[0] == "v") {
                if (w.size() == 5) {
                    double h = real(w[4]);
                    require(std::abs(h) > 1e-20, QStringLiteral("OBJ 齐次坐标 w 为零。"));
                    v /= float(h);
                }
                require(finite(v) && positions.size() < MaxVertices,
                        QStringLiteral("OBJ 坐标或顶点数量超限。"));
                positions << v;
                if (w.size() >= 7)
                    warning(warnings, QStringLiteral("OBJ 顶点颜色未保留，使用材质／对象单色。"));
            } else {
                require(finite(v) && normals.size() < MaxVertices && !v.isNull(),
                        QStringLiteral("OBJ 法线无效。"));
                normals << v.normalized();
            }
        } else if (w[0] == "vt") {
            warning(warnings, QStringLiteral("OBJ UV 未保留，当前显示对象单色。"));
            require(w.size() >= 2, QStringLiteral("OBJ UV 缺失。"));
            for (int i = 1; i < w.size(); ++i)
                real(w[i]);
            require(++texcoords <= MaxVertices, QStringLiteral("OBJ UV 数量超限。"));
        } else if (w[0] == "f") {
            QVector<ObjCorner> corners;
            require(w.size() <= 1025, QStringLiteral("OBJ 多边形过大。"));
            for (int i = 1; i < w.size(); ++i) {
                auto s = w[i].split('/');
                require(s.size() <= 3, QStringLiteral("OBJ 面格式无效。"));
                ObjCorner c;
                c.position = objIndex(s[0], positions.size());
                if (s.size() > 1 && !s[1].isEmpty())
                    objIndex(s[1], texcoords);
                if (s.size() > 2 && !s[2].isEmpty())
                    c.normal = objIndex(s[2], normals.size());
                corners << c;
            }
            polygon(mesh, corners, positions, normals);
        } else if (w[0] == "o" || w[0] == "g") {
            flush();
            name = line.mid(1).trimmed();
            if (name.isEmpty())
                name = QStringLiteral("OBJ 网格");
        } else if (w[0] == "usemtl") {
            flush();
            material = line.mid(6).trimmed();
        } else if (w[0] == "mtllib") { /* resolved before geometry */
        } else if (w[0] != "s")
            warning(warnings, QStringLiteral("OBJ 中非面元素／扩展未导入：") + w[0]);
    }
    flush();
    validateImported(result);
    return result;
}
} // namespace

Body makeMeshBody(const QString &name, const Mesh &mesh, const QColor &color) {
    require(!mesh.vertices.isEmpty() && mesh.vertices.size() % 3 == 0 && mesh.vertices.size() <= MaxVertices,
            QStringLiteral("网格为空或三角形数量无效。"));
    auto data = std::make_shared<SolidData>();
    for (int i = 0; i < mesh.vertices.size(); i += 3)
        triangle(data->mesh, mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]);
    for (int i = 0; i < data->mesh.vertices.size(); i += 3) {
        auto a = data->mesh.vertices[i], b = data->mesh.vertices[i + 1], c = data->mesh.vertices[i + 2];
        data->edgeVertices << a << b << b << c << c << a;
        data->bounds.include(a.position);
        data->bounds.include(b.position);
        data->bounds.include(c.position);
        data->area += .5 * QVector3D::crossProduct(b.position - a.position, c.position - a.position).length();
    }
    Body body;
    body.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    body.name = name.trimmed().isEmpty() ? QStringLiteral("网格模型") : name.left(160);
    body.shape = Shape::Mesh;
    body.color = color;
    body.solid = data;
    return body;
}
QByteArray encodeMesh(const Body &body) {
    const auto data = solidData(body);
    QByteArray raw;
    raw.reserve(4 + data->mesh.vertices.size() * 24);
    addU32(raw, quint32(data->mesh.vertices.size()));
    for (const auto &v : data->mesh.vertices)
        for (float f :
             {v.position.x(), v.position.y(), v.position.z(), v.normal.x(), v.normal.y(), v.normal.z()})
            addFloat(raw, f);
    return qCompress(raw, 6).toBase64();
}
bool decodeMesh(const QByteArray &bytes, Body &body, QString *error, int maxVertices) {
    try {
        require(maxVertices > 0 && maxVertices <= MaxVertices, QStringLiteral("场景网格总量超过限制。"));
        auto decoded = QByteArray::fromBase64Encoding(bytes, QByteArray::AbortOnBase64DecodingErrors);
        require(bool(decoded) && decoded.decoded.size() >= 4, QStringLiteral("网格 Base64 无效。"));
        quint32 length =
            qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(decoded.decoded.constData()));
        require(length >= 4 && length <= quint32(4 + maxVertices * 24), QStringLiteral("网格解压大小超限。"));
        auto raw = qUncompress(decoded.decoded);
        require(raw.size() == int(length), QStringLiteral("网格解压长度不匹配。"));
        quint32 count = u32(raw, 0);
        require(count > 0 && count <= quint32(maxVertices) && count % 3 == 0 &&
                    qint64(count) * 24 + 4 == raw.size(),
                QStringLiteral("网格载荷长度无效。"));
        Mesh mesh;
        mesh.vertices.reserve(int(count));
        for (quint32 i = 0; i < count; ++i) {
            float f[6];
            for (int c = 0; c < 6; ++c) {
                quint32 bits = u32(raw, 4 + int(i) * 24 + c * 4);
                std::memcpy(&f[c], &bits, 4);
            }
            mesh.vertices << Vertex{{f[0], f[1], f[2]}, {f[3], f[4], f[5]}};
        }
        auto candidate = makeMeshBody(body.name, mesh, body.color);
        body.solid = candidate.solid;
        body.shape = Shape::Mesh;
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
bool importMeshFile(const QString &path, QVector<Body> &bodies, QString *error, QStringList *warnings) {
    try {
        QStringList notes;
        auto bytes = readFile(path);
        auto suffix = QFileInfo(path).suffix().toLower();
        require(suffix == "glb" || suffix == "obj", QStringLiteral("请选择 .glb 或 .obj 文件。"));
        auto result = suffix == "glb" ? readGlb(bytes, notes) : readObj(path, bytes, notes);
        bodies = std::move(result);
        if (warnings)
            *warnings = notes;
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
bool exportGlb(const QString &path, const Scene &scene, QString *error) {
    try {
        validScene(scene);
        QByteArray bin;
        QJsonArray nodes, meshes, materials, views, accessors, roots;
        for (const auto &body : scene.bodies)
            if (body.visible) {
                const auto mesh = tessellate(body);
                int count = mesh.vertices.size();
                require(qint64(bin.size()) + qint64(count) * 24 <= MaxBytes,
                        QStringLiteral("GLB 输出过大。"));
                auto model = body.matrix();
                Bounds bounds;
                int offset = bin.size();
                for (const auto &v : mesh.vertices) {
                    auto p = toGltf(model.map(v.position)) * .001f;
                    bounds.include(p);
                    for (float f : {p.x(), p.y(), p.z()})
                        addFloat(bin, f);
                }
                int positionView = views.size();
                views << QJsonObject{
                    {"buffer", 0}, {"byteOffset", offset}, {"byteLength", count * 12}, {"target", 34962}};
                int positionAccessor = accessors.size();
                accessors << QJsonObject{{"bufferView", positionView},
                                         {"componentType", 5126},
                                         {"count", count},
                                         {"type", "VEC3"},
                                         {"min", QJsonArray{bounds.min.x(), bounds.min.y(), bounds.min.z()}},
                                         {"max", QJsonArray{bounds.max.x(), bounds.max.y(), bounds.max.z()}}};
                offset = bin.size();
                for (const auto &v : mesh.vertices) {
                    auto n = toGltf(body.rotation.rotatedVector(v.normal)).normalized();
                    for (float f : {n.x(), n.y(), n.z()})
                        addFloat(bin, f);
                }
                int normalView = views.size();
                views << QJsonObject{
                    {"buffer", 0}, {"byteOffset", offset}, {"byteLength", count * 12}, {"target", 34962}};
                int normalAccessor = accessors.size();
                accessors << QJsonObject{
                    {"bufferView", normalView}, {"componentType", 5126}, {"count", count}, {"type", "VEC3"}};
                int mat = materials.size();
                materials << QJsonObject{
                    {"name", body.name},
                    {"doubleSided", true},
                    {"pbrMetallicRoughness",
                     QJsonObject{{"baseColorFactor",
                                  QJsonArray{linear(body.color.redF()), linear(body.color.greenF()),
                                             linear(body.color.blueF()), 1.0}},
                                 {"metallicFactor", 0.0},
                                 {"roughnessFactor", .6}}}};
                int meshIndex = meshes.size();
                meshes << QJsonObject{
                    {"name", body.name},
                    {"primitives",
                     QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", positionAccessor},
                                                                       {"NORMAL", normalAccessor}}},
                                            {"mode", 4},
                                            {"material", mat}}}}};
                roots << nodes.size();
                nodes << QJsonObject{{"name", body.name}, {"mesh", meshIndex}};
            }
        require(!nodes.isEmpty(), QStringLiteral("没有可导出的可见模型。"));
        QJsonObject root{{"asset", QJsonObject{{"version", "2.0"}, {"generator", "AxisForge CAD"}}},
                         {"scene", 0},
                         {"scenes", QJsonArray{QJsonObject{{"nodes", roots}}}},
                         {"nodes", nodes},
                         {"meshes", meshes},
                         {"materials", materials},
                         {"buffers", QJsonArray{QJsonObject{{"byteLength", bin.size()}}}},
                         {"bufferViews", views},
                         {"accessors", accessors}};
        auto json = QJsonDocument(root).toJson(QJsonDocument::Compact);
        while (json.size() % 4)
            json.append(' ');
        while (bin.size() % 4)
            bin.append('\0');
        QByteArray out;
        addU32(out, 0x46546c67);
        addU32(out, 2);
        addU32(out, quint32(28 + json.size() + bin.size()));
        addU32(out, quint32(json.size()));
        addU32(out, 0x4e4f534a);
        out += json;
        addU32(out, quint32(bin.size()));
        addU32(out, 0x004e4942);
        out += bin;
        writeFile(path, out);
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
bool exportObj(const QString &path, const Scene &scene, QString *error) {
    QString committedMtl;
    try {
        validScene(scene);
        QSaveFile file(path);
        require(file.open(QIODevice::WriteOnly), QStringLiteral("无法创建 OBJ。"));
        QTextStream out(&file);
        utf8(out);
        out.setRealNumberPrecision(9);
        QString mtlName = "axisforge-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".mtl";
        QByteArray mtl;
        QTextStream materials(&mtl, QIODevice::WriteOnly);
        utf8(materials);
        materials.setRealNumberPrecision(9);
        out << "# AxisForge CAD | units: mm | Z-up | visible bodies only\nmtllib " << mtlName << "\n";
        qint64 first = 1;
        int index = 0;
        for (const auto &body : scene.bodies)
            if (body.visible) {
                auto mesh = tessellate(body);
                QString name = body.name;
                name.replace('\n', ' ');
                name.replace('\r', ' ');
                name.replace('#', '_');
                out << "o " << name << "\nusemtl m" << index << "\n";
                materials << "newmtl m" << index << "\nKd " << body.color.redF() << ' ' << body.color.greenF()
                          << ' ' << body.color.blueF() << "\n\n";
                const auto matrix = body.matrix();
                int n = 0;
                for (const auto &v : mesh.vertices) {
                    auto p = matrix.map(v.position),
                         normal = body.rotation.rotatedVector(v.normal).normalized();
                    out << "v " << p.x() << ' ' << p.y() << ' ' << p.z() << "\nvn " << normal.x() << ' '
                        << normal.y() << ' ' << normal.z() << '\n';
                    if (++n % 2048 == 0) {
                        out.flush();
                        require(file.pos() <= MaxBytes, QStringLiteral("OBJ 输出超过 64 MiB。"));
                    }
                }
                for (int i = 0; i < mesh.vertices.size(); i += 3) {
                    out << "f";
                    for (int j = 0; j < 3; ++j)
                        out << ' ' << first + i + j << "//" << first + i + j;
                    out << '\n';
                    if (i % 6144 == 0) {
                        out.flush();
                        require(file.pos() <= MaxBytes, QStringLiteral("OBJ 输出超过 64 MiB。"));
                    }
                }
                first += mesh.vertices.size();
                ++index;
            }
        require(index > 0, QStringLiteral("没有可导出的可见模型。"));
        out.flush();
        materials.flush();
        require(out.status() == QTextStream::Ok && file.pos() <= MaxBytes,
                QStringLiteral("OBJ 写入失败或超限。"));
        const auto mtlPath = QFileInfo(path).absoluteDir().filePath(mtlName);
        require(!QFileInfo::exists(mtlPath), QStringLiteral("材质输出文件已存在，请重试。"));
        writeFile(mtlPath, mtl);
        committedMtl = mtlPath;
        require(file.commit(), QStringLiteral("OBJ 原子提交失败。"));
        committedMtl.clear();
        return true;
    } catch (const std::exception &e) {
        if (!committedMtl.isEmpty())
            QFile::remove(committedMtl);
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
} // namespace af
