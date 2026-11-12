#include "ProjectIO.h"
#include "Constraints.h"
#include "Geometry.h"
#include "Kernel.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

namespace af {
namespace {
QJsonArray vec(const QVector3D &v) {
    return {v.x(), v.y(), v.z()};
}
double number(const QJsonObject &o, const QString &key) {
    auto v = o.value(key);
    if (!v.isDouble() || !std::isfinite(v.toDouble()))
        throw std::runtime_error("Expected a finite number");
    return v.toDouble();
}
QString string(const QJsonObject &o, const QString &key) {
    auto v = o.value(key);
    if (!v.isString())
        throw std::runtime_error("Expected a string");
    return v.toString();
}
bool boolean(const QJsonObject &o, const QString &key) {
    auto v = o.value(key);
    if (!v.isBool())
        throw std::runtime_error("Expected a boolean");
    return v.toBool();
}
QVector<double> array(const QJsonObject &o, const QString &key, int count) {
    auto v = o.value(key);
    if (!v.isArray() || v.toArray().size() != count)
        throw std::runtime_error("Invalid vector length");
    QVector<double> result;
    for (auto a : v.toArray()) {
        if (!a.isDouble() || !std::isfinite(a.toDouble()))
            throw std::runtime_error("Invalid vector component");
        result << a.toDouble();
    }
    return result;
}
int integer(const QJsonObject &o, const QString &key) {
    double v = number(o, key);
    if (v != std::floor(v) || v < -1000000 || v > 1000000)
        throw std::runtime_error("Expected an integer");
    return int(v);
}
} // namespace
QByteArray serialize(const Scene &s) {
    QJsonArray bodies, mates;
    for (const auto &b : s.bodies) {
        const auto &p = b.params;
        auto q = b.rotation;
        QJsonObject object{{"id", b.id},
                           {"name", b.name},
                           {"shape", shapeKey(b.shape)},
                           {"parameters", QJsonObject{{"width", p.width},
                                                      {"depth", p.depth},
                                                      {"height", p.height},
                                                      {"radius", p.radius},
                                                      {"tubeRadius", p.tubeRadius},
                                                      {"segments", p.segments}}},
                           {"position", vec(b.position)},
                           {"rotation", QJsonArray{q.scalar(), q.x(), q.y(), q.z()}},
                           {"color", b.color.name(QColor::HexRgb)},
                           {"visible", b.visible},
                           {"fixed", b.fixed}};
        if (b.shape == Shape::Solid)
            object["brep"] = QString::fromLatin1(encodeBrep(b));
        bodies.append(object);
    }
    for (const auto &m : s.mates)
        mates.append(QJsonObject{{"id", m.id},
                                 {"source", m.source},
                                 {"target", m.target},
                                 {"type", mateKey(m.type)},
                                 {"sourceAxis", m.sourceAxis},
                                 {"targetAxis", m.targetAxis},
                                 {"distance", m.distance},
                                 {"enabled", m.enabled}});
    return QJsonDocument(QJsonObject{{"format", "axisforge.project"},
                                     {"version", 2},
                                     {"units", "mm"},
                                     {"title", s.title},
                                     {"bodies", bodies},
                                     {"mates", mates}})
        .toJson(QJsonDocument::Indented);
}
bool deserialize(const QByteArray &bytes, Scene &scene, QString *error) {
    auto fail = [&](QString s) {
        if (error)
            *error = s;
        return false;
    };
    if (bytes.size() > 64 * 1024 * 1024)
        return fail(QStringLiteral("项目文件超过 64 MiB 限制。"));
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("JSON 项目格式无效：") + parse.errorString());
    try {
        const auto root = doc.object();
        if (string(root, "format") != "axisforge.project" ||
            (integer(root, "version") != 1 && integer(root, "version") != 2) || string(root, "units") != "mm")
            return fail(QStringLiteral("不支持的项目格式、版本或单位。"));
        if (!root["bodies"].isArray() || !root["mates"].isArray())
            return fail(QStringLiteral("项目缺少模型或约束列表。"));
        if (root["bodies"].toArray().size() > 300 || root["mates"].toArray().size() > 1000)
            return fail(QStringLiteral("项目对象数量超过限制。"));
        Scene result;
        result.title = string(root, "title");
        for (auto item : root["bodies"].toArray()) {
            if (!item.isObject())
                throw std::runtime_error("Invalid body");
            auto o = item.toObject();
            Body b;
            b.id = string(o, "id");
            b.name = string(o, "name");
            int shape = QStringList{"box", "sphere", "cylinder", "cone", "torus", "solid"}.indexOf(
                string(o, "shape"));
            if (shape < 0)
                throw std::runtime_error("Unknown shape");
            b.shape = Shape(shape);
            if (!o["parameters"].isObject())
                throw std::runtime_error("Missing parameters");
            auto p = o["parameters"].toObject();
            b.params.width = number(p, "width");
            b.params.depth = number(p, "depth");
            b.params.height = number(p, "height");
            b.params.radius = number(p, "radius");
            b.params.tubeRadius = number(p, "tubeRadius");
            b.params.segments = integer(p, "segments");
            auto v = array(o, "position", 3), q = array(o, "rotation", 4);
            b.position = {float(v[0]), float(v[1]), float(v[2])};
            b.rotation = QQuaternion(float(q[0]), float(q[1]), float(q[2]), float(q[3]));
            b.color = QColor(string(o, "color"));
            b.visible = boolean(o, "visible");
            b.fixed = boolean(o, "fixed");
            if (b.shape == Shape::Solid) {
                if (integer(root, "version") < 2)
                    throw std::runtime_error("实体需要项目格式version 2。");
                QString why;
                if (!decodeBrep(string(o, "brep").toLatin1(), b, &why))
                    throw std::runtime_error(why.toStdString());
            }
            result.bodies << b;
        }
        for (auto item : root["mates"].toArray()) {
            if (!item.isObject())
                throw std::runtime_error("Invalid mate");
            auto o = item.toObject();
            Mate m;
            m.id = string(o, "id");
            m.source = string(o, "source");
            m.target = string(o, "target");
            int type =
                QStringList{"coaxial", "parallel", "perpendicular", "distance"}.indexOf(string(o, "type"));
            if (type < 0)
                throw std::runtime_error("Unknown mate");
            m.type = MateType(type);
            m.sourceAxis = integer(o, "sourceAxis");
            m.targetAxis = integer(o, "targetAxis");
            m.distance = number(o, "distance");
            m.enabled = boolean(o, "enabled");
            result.mates << m;
        }
        if (!solve(result, error))
            return false;
        scene = result;
        return true;
    } catch (const std::exception &e) {
        return fail(QStringLiteral("项目字段无效：") + QString::fromUtf8(e.what()));
    }
}
bool saveProject(const QString &path, const Scene &scene, QString *error) {
    Scene copy = scene;
    if (!solve(copy, error))
        return false;
    const auto bytes = serialize(copy);
    if (bytes.size() > 64 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("项目输出超过64 MiB限制。");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
bool loadProject(const QString &path, Scene &scene, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.size() > 64 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("项目文件超过 64 MiB 限制。");
        return false;
    }
    return deserialize(file.readAll(), scene, error);
}
bool exportMesh(const QString &path, const Scene &scene, bool stl, QString *error) {
    if (!scene.validate(error))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out.setLocale(QLocale::c());
    out.setRealNumberPrecision(9);
    qint64 offset = 1;
    if (stl)
        out << "solid AxisForge\n";
    else
        out << "# AxisForge CAD | units: mm | visible bodies only\n";
    for (const auto &body : scene.bodies) {
        if (!body.visible)
            continue;
        auto mesh = tessellate(body);
        auto matrix = body.matrix();
        if (!stl) {
            QString name = body.name;
            name.replace(QRegularExpression("[^a-zA-Z0-9_-]"), "_");
            out << "o " << name << "\n";
            for (const auto &v : mesh.vertices) {
                auto p = matrix.map(v.position);
                out << "v " << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
            }
            for (int i = 0; i < mesh.vertices.size(); i += 3)
                out << "f " << offset + i << ' ' << offset + i + 1 << ' ' << offset + i + 2 << '\n';
            offset += mesh.vertices.size();
        } else
            for (int i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                QVector3D p[3] = {matrix.map(mesh.vertices[i].position),
                                  matrix.map(mesh.vertices[i + 1].position),
                                  matrix.map(mesh.vertices[i + 2].position)};
                auto normal = QVector3D::crossProduct(p[1] - p[0], p[2] - p[0]).normalized();
                out << "facet normal " << normal.x() << ' ' << normal.y() << ' ' << normal.z()
                    << "\nouter loop\n";
                for (auto point : p)
                    out << "vertex " << point.x() << ' ' << point.y() << ' ' << point.z() << '\n';
                out << "endloop\nendfacet\n";
            }
    }
    if (stl)
        out << "endsolid AxisForge\n";
    out.flush();
    if (out.status() != QTextStream::Ok || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
} // namespace af
