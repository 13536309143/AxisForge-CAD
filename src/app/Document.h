#pragma once
#include "core/Scene.h"
#include <QObject>
#include <QUndoStack>
#include <functional>
namespace af {
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
  signals:
    void changed();
    void rejected(const QString &message);

  private:
    friend class SnapshotCommand;
    Scene scene_;
    QUndoStack history_;
};
} // namespace af
