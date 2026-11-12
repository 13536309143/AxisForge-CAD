#pragma once
#include "core/MeshIO.h"
#include <QtEndian>
#include <cstring>
namespace meshfixture {
inline void put32(QByteArray &b, quint32 v) {
    char raw[4];
    qToLittleEndian(v, reinterpret_cast<uchar *>(raw));
    b.append(raw, 4);
}
inline void putFloat(QByteArray &b, float v) {
    quint32 bits;
    std::memcpy(&bits, &v, 4);
    put32(b, bits);
}
inline void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly));
    CHECK(f.write(bytes) == bytes.size());
}
inline QByteArray glb(QJsonObject root, QByteArray bin) {
    auto json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    while (json.size() % 4)
        json += ' ';
    while (bin.size() % 4)
        bin += '\0';
    QByteArray b;
    put32(b, 0x46546c67);
    put32(b, 2);
    put32(b, 28 + json.size() + bin.size());
    put32(b, json.size());
    put32(b, 0x4e4f534a);
    b += json;
    put32(b, bin.size());
    put32(b, 0x004e4942);
    b += bin;
    return b;
}
inline QJsonObject root(QByteArray &bin) {
    bin.clear();
    for (float v : {0.f, 0.f, 0.f, .02f, 0.f, 0.f, 0.f, .03f, 0.f})
        putFloat(bin, v);
    bin.append(char(0));
    bin.append(char(1));
    bin.append(char(2));
    return QJsonObject{
        {"asset", QJsonObject{{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
        {"nodes",
         QJsonArray{QJsonObject{
             {"name", QStringLiteral("三角形")}, {"mesh", 0}, {"translation", QJsonArray{.1, .2, .3}}}}},
        {"meshes", QJsonArray{QJsonObject{
                       {"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 0}}},
                                                             {"indices", 1}}}}}}},
        {"buffers", QJsonArray{QJsonObject{{"byteLength", bin.size()}}}},
        {"bufferViews", QJsonArray{QJsonObject{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                                   QJsonObject{{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 3}}}},
        {"accessors",
         QJsonArray{
             QJsonObject{{"bufferView", 0},
                         {"componentType", 5126},
                         {"count", 3},
                         {"type", "VEC3"},
                         {"min", QJsonArray{0, 0, 0}},
                         {"max", QJsonArray{.02, .03, 0}}},
             QJsonObject{{"bufferView", 1}, {"componentType", 5121}, {"count", 3}, {"type", "SCALAR"}}}}};
}
inline QVector3D worldPoint(const Body &b, int i) {
    return b.matrix().map(tessellate(b).vertices[i].position);
}
inline double area(const QVector<Body> &bodies) {
    double result = 0;
    for (const auto &b : bodies)
        result += surfaceArea(b);
    return result;
}
} // namespace meshfixture
