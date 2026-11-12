#pragma once
#include "core/Scene.h"
#include <QObject>
#include <QStringList>
#include <QUndoStack>
#include <functional>
#include <memory>
namespace af {
enum class BooleanOp;
class Document : public QObject {
    Q_OBJECT
  public:
    explicit Document(QObject *parent = nullptr);
    const Scene &scene() const {
        return scene_;
    }
    QUndoStack *history() {
        return &history_;
    }
    bool edit(const QString &label, const std::function<void(Scene &)> &operation, QString *error = nullptr);
    void replace(const Scene &scene);
    QString addBody(Shape shape);
    void removeBody(const QString &id);
    QString duplicateBody(const QString &id);
    QString booleanOperation(const QString &a, const QString &b, BooleanOp op, bool hideOperands = true,
                             QString *error = nullptr);
    bool importStepFile(const QString &path, QString *error = nullptr);
    bool importMesh(const QString &path, QString *error = nullptr, QStringList *warnings = nullptr);
    void beginMove(const QString &id);
    void previewMove(const QString &id, const QVector3D &position);
    void finishMove(bool commit);
  signals:
    void changed();
    void rejected(const QString &message);

  private:
    friend class SnapshotCommand;
    Scene scene_;
    QUndoStack history_;
    std::unique_ptr<Scene> moveBefore_;
    QString movingId_;
};
} // namespace af
