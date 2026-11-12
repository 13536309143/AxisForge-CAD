#pragma once
#include "Scene.h"
#include <QByteArray>
namespace af {
QByteArray serialize(const Scene &scene);
bool deserialize(const QByteArray &bytes, Scene &scene, QString *error = nullptr);
bool saveProject(const QString &path, const Scene &scene, QString *error = nullptr);
bool loadProject(const QString &path, Scene &scene, QString *error = nullptr);
bool exportMesh(const QString &path, const Scene &scene, bool stl, QString *error = nullptr);
} // namespace af
