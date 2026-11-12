#include "Document.h"
#include "core/Constraints.h"
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
    if (!scene_.find(id))
        return;
    edit(QStringLiteral("删除模型"), [&](Scene &s) {
        s.bodies.erase(
            std::remove_if(s.bodies.begin(), s.bodies.end(), [&](const Body &b) { return b.id == id; }),
            s.bodies.end());
        s.mates.erase(std::remove_if(s.mates.begin(), s.mates.end(),
                                     [&](const Mate &m) { return m.source == id || m.target == id; }),
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
} // namespace af
