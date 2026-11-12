#pragma once
#include "Kernel.h"
#include <QStringList>
namespace af {
// Meshes remain meshes: no inferred watertightness or OCCT solid topology.
Body makeMeshBody(const QString &name, const Mesh &mesh, const QColor &color = QColor("#b8c9d8"));
QByteArray encodeMesh(const Body &body);
bool decodeMesh(const QByteArray &bytes, Body &body, QString *error = nullptr, int maxVertices = 3000000);
bool importMeshFile(const QString &path, QVector<Body> &bodies, QString *error = nullptr,
                    QStringList *warnings = nullptr);
bool exportGlb(const QString &path, const Scene &scene, QString *error = nullptr);
bool exportObj(const QString &path, const Scene &scene, QString *error = nullptr);
} // namespace af
