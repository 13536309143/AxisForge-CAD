#include "Document.h"
#include "core/Constraints.h"
#include "core/Kernel.h"
#include "core/ProjectIO.h"
#include <QUndoCommand>
#include <QUuid>
#include <algorithm>
namespace af {
class SnapshotCommand final : public QUndoCommand {
  public:
    SnapshotCommand(Document *doc, Scene before, Scene after, const QString &label)
        : doc_(doc), before_(std::move(before)), after_(std::move(after)) {
        setText(label);
    }
    void undo() override {
        doc_->scene_ = before_;
        emit doc_->changed();
    }
    void redo() override {
        doc_->scene_ = after_;
        emit doc_->changed();
    }

  private:
    Document *doc_;
    Scene before_, after_;
};
Document::Document(QObject *parent) : QObject(parent) {
    history_.setUndoLimit(100);
}
bool Document::edit(const QString &label, const std::function<void(Scene &)> &operation, QString *error) {
    finishMove(false);
    Scene candidate = scene_;
    operation(candidate);
    QString why;
    if (!solve(candidate, &why)) {
        if (error)
            *error = why;
        emit rejected(why);
        return false;
    }
    history_.push(new SnapshotCommand(this, scene_, candidate, label));
    return true;
}
void Document::replace(const Scene &scene) {
    moveBefore_.reset();
    movingId_.clear();
    scene_ = scene;
    history_.clear();
    history_.setClean();
    emit changed();
}
QString Document::addBody(Shape shape) {
    Body b = makeBody(shape, scene_.bodies.size() + 1);
    b.position = {float((scene_.bodies.size() % 4) * 38 - 50), float((scene_.bodies.size() / 4) * 38), 20};
    if (!edit(QStringLiteral("创建 ") + shapeName(shape), [&](Scene &s) { s.bodies << b; }))
        return {};
    return b.id;
}
void Document::removeBody(const QString &id) {
    const QString bodyId = id;
    if (!scene_.find(bodyId))
        return;
    edit(QStringLiteral("删除模型"), [&](Scene &s) {
        s.bodies.erase(
            std::remove_if(s.bodies.begin(), s.bodies.end(), [&](const Body &b) { return b.id == bodyId; }),
            s.bodies.end());
        s.mates.erase(std::remove_if(s.mates.begin(), s.mates.end(),
                                     [&](const Mate &m) { return m.source == bodyId || m.target == bodyId; }),
                      s.mates.end());
    });
}
QString Document::duplicateBody(const QString &id) {
    auto source = scene_.find(id);
    if (!source)
        return {};
    Body copy = *source;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name += QStringLiteral(" · 副本");
    copy.position += QVector3D(25, 25, 0);
    copy.fixed = false;
    if (!edit(QStringLiteral("复制模型"), [&](Scene &s) { s.bodies << copy; }))
        return {};
    return copy.id;
}
void Document::beginMove(const QString &id) {
    finishMove(false);
    const auto *b = scene_.find(id);
    if (!b || b->fixed)
        return;
    movingId_ = id;
    moveBefore_ = std::make_unique<Scene>(scene_);
}
QString Document::booleanOperation(const QString &a, const QString &b, BooleanOp op, bool hideOperands,
                                   QString *error) {
    finishMove(false);
    auto left = scene_.find(a), right = scene_.find(b);
    if (!left || !right) {
        if (error)
            *error = QStringLiteral("请选择两个存在的模型。");
        return {};
    }
    Body result;
    QString why;
    if (!booleanBody(*left, *right, op, result, &why)) {
        if (error)
            *error = why;
        emit rejected(why);
        return {};
    }
    const QString aid = a, bid = b;
    if (!edit(
            QStringLiteral("OCCT 布尔运算"),
            [&](Scene &s) {
                if (hideOperands) {
                    s.find(aid)->visible = false;
                    s.find(bid)->visible = false;
                }
                s.bodies << result;
            },
            error))
        return {};
    return result.id;
}
bool Document::importStepFile(const QString &path, QString *error) {
    QVector<Body> imported;
    QString why;
    if (!importStep(path, imported, &why)) {
        if (error)
            *error = why;
        emit rejected(why);
        return false;
    }
    return edit(QStringLiteral("导入 STEP 实体"), [&](Scene &s) { s.bodies += imported; }, error);
}
void Document::previewMove(const QString &id, const QVector3D &position) {
    if (!moveBefore_ || id != movingId_)
        return;
    Scene candidate = *moveBefore_;
    candidate.find(id)->position = position;
    QString why;
    if (!solve(candidate, &why)) {
        emit rejected(why);
        return;
    }
    scene_ = candidate;
    emit changed();
}
void Document::finishMove(bool commit) {
    if (!moveBefore_)
        return;
    Scene before = *moveBefore_, after = scene_;
    moveBefore_.reset();
    movingId_.clear();
    scene_ = before;
    if (commit && serialize(before) != serialize(after))
        history_.push(new SnapshotCommand(this, before, after, QStringLiteral("拖动模型")));
    else
        emit changed();
}
} // namespace af
