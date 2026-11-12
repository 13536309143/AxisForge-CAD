#include "MainWindow.h"
#include "core/Constraints.h"
#include "core/Presentation.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>
namespace af {
void MainWindow::selectMate(const QString &id) {
    if (mateEditor_)
        return;
    const auto *m = findMate(document_.scene(), id);
    mateSelection_ = m ? id : QString();
    selection_.clear();
    refreshing_ = true;
    tree_->clearSelection();
    mateTree_->clearSelection();
    if (!m)
        mateTree_->setCurrentItem(nullptr);
    for (int i = 0; i < mateTree_->topLevelItemCount(); ++i) {
        auto *item = mateTree_->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == mateSelection_) {
            mateTree_->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    showProperties();
    viewport_->setSelection({});
    viewport_->setMateSelection(mateSelection_);
    if (m) {
        const auto g = mateGuide(document_.scene(), *m);
        mateInfo_->setText(g.title + "\n" + g.detail + QStringLiteral("\n双击编辑；青色为 A，橙色为 B。"));
    } else
        mateInfo_->setText(QStringLiteral("选择配合可查看轴线、距离与角度；双击编辑。"));
    refreshing_ = false;
}
void MainWindow::addMateDialog() {
    openMateEditor({});
}
void MainWindow::openMateEditor(const QString &editId) {
    if (mateEditor_) {
        mateEditor_->show();
        mateEditor_->raise();
        mateEditor_->activateWindow();
        return;
    }
    if (document_.scene().bodies.size() < 2) {
        statusBar()->showMessage(QStringLiteral("至少创建两个模型才能添加配合。"), 5000);
        return;
    }
    document_.finishMove(false);
    const auto *existing = findMate(document_.scene(), editId);
    if (!editId.isEmpty() && !existing)
        return;
    const QString previousSelection = mateSelection_;
    struct Session {
        int pick = 0;
        bool committing = false, applied = false;
        Mate draft;
    };
    auto state = std::make_shared<Session>();
    state->draft = existing ? *existing : Mate{};
    if (!existing)
        state->draft.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *dialog = new QDialog(this);
    mateEditor_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("mateDialog");
    dialog->setWindowTitle(existing ? QStringLiteral("编辑装配配合") : QStringLiteral("添加装配配合"));
    dialog->setWindowModality(Qt::NonModal);
    dialog->setMinimumWidth(430);
    auto *layout = new QVBoxLayout(dialog);
    auto *info = new QLabel(QStringLiteral(
        "A 驱动 B，基准使用模型原点和局部轴。可在场景点选对象；窗口保持打开时只选择，不拖动。"));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *pickRow = new QHBoxLayout;
    auto *pickPair = new QPushButton(QStringLiteral("场景点选 A → B"));
    pickPair->setObjectName("matePickPair");
    auto *pickA = new QPushButton(QStringLiteral("选 A")), *pickB = new QPushButton(QStringLiteral("选 B")),
         *swap = new QPushButton(QStringLiteral("交换 A/B"));
    pickA->setObjectName("matePickSource");
    pickB->setObjectName("matePickTarget");
    pickRow->addWidget(pickPair);
    pickRow->addWidget(pickA);
    pickRow->addWidget(pickB);
    pickRow->addWidget(swap);
    layout->addLayout(pickRow);
    auto *form = new QFormLayout;
    auto *source = new QComboBox, *target = new QComboBox, *type = new QComboBox, *sa = new QComboBox,
         *ta = new QComboBox;
    source->setObjectName("mateSource");
    target->setObjectName("mateTarget");
    type->setObjectName("mateType");
    sa->setObjectName("mateSourceAxis");
    ta->setObjectName("mateTargetAxis");
    for (const auto &b : document_.scene().bodies) {
        source->addItem(b.name, b.id);
        target->addItem(b.name, b.id);
    }
    source->setCurrentIndex(qMax(0, source->findData(existing ? existing->source : selection_)));
    target->setCurrentIndex(existing ? target->findData(existing->target)
                                     : (source->currentIndex() == 0 ? 1 : 0));
    for (int i = 0; i < 7; ++i)
        type->addItem(mateName(MateType(i)), i);
    type->setCurrentIndex(int(state->draft.type));
    sa->addItems({"X", "Y", "Z"});
    ta->addItems({"X", "Y", "Z"});
    sa->setCurrentIndex(state->draft.sourceAxis);
    ta->setCurrentIndex(state->draft.targetAxis);
    auto spin = [&](const char *name, double min, double max, const QString &suffix, double value) {
        auto *s = new QDoubleSpinBox;
        s->setObjectName(name);
        s->setRange(min, max);
        s->setDecimals(3);
        s->setSuffix(suffix);
        s->setValue(value);
        return s;
    };
    auto *distance = spin("mateDistance", 0, 1000000, " mm", state->draft.distance),
         *angle = spin("mateAngle", 0, 180, "°", state->draft.angle),
         *offset = spin("mateOffset", -1000000, 1000000, " mm", state->draft.offset);
    form->addRow(QStringLiteral("基准 A"), source);
    form->addRow(QStringLiteral("从动 B"), target);
    form->addRow(QStringLiteral("配合类型"), type);
    form->addRow(QStringLiteral("A 局部轴"), sa);
    form->addRow(QStringLiteral("B 局部轴"), ta);
    form->addRow(QStringLiteral("中心距离"), distance);
    form->addRow(QStringLiteral("指定夹角"), angle);
    form->addRow(QStringLiteral("沿轴偏移"), offset);
    layout->addLayout(form);
    auto *enabled = new QCheckBox(QStringLiteral("启用此配合"));
    enabled->setChecked(state->draft.enabled);
    enabled->setObjectName("mateEnabled");
    layout->addWidget(enabled);
    auto *preview = new QCheckBox(QStringLiteral("实时预览位置与姿态（应用前不修改文档）"));
    preview->setChecked(true);
    preview->setObjectName("matePreview");
    layout->addWidget(preview);
    auto *explanation = new QLabel;
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto *feedback = new QLabel;
    feedback->setObjectName("mateFeedback");
    feedback->setWordWrap(true);
    layout->addWidget(feedback);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("应用配合"));
    buttons->button(QDialogButtonBox::Save)->setObjectName("mateApply");
    layout->addWidget(buttons);
    auto read = [=] {
        Mate m = state->draft;
        m.source = source->currentData().toString();
        m.target = target->currentData().toString();
        m.type = MateType(type->currentData().toInt());
        m.sourceAxis = sa->currentIndex();
        m.targetAxis = ta->currentIndex();
        m.distance = distance->value();
        m.angle = angle->value();
        m.offset = offset->value();
        m.enabled = enabled->isChecked();
        return m;
    };
    auto update = [=] {
        if (state->committing)
            return;
        const auto m = read();
        const auto t = m.type;
        distance->setEnabled(t == MateType::Distance);
        angle->setEnabled(t == MateType::Angle);
        offset->setEnabled(t == MateType::AxialOffset);
        sa->setEnabled(t != MateType::Distance && t != MateType::Coincident);
        ta->setEnabled(t != MateType::Distance && t != MateType::Coincident && t != MateType::AxialOffset);
        const QStringList help = {
            QStringLiteral("同轴：轴线重合，保留沿轴滑动和绕轴旋转。"),
            QStringLiteral("平行：包含反平行，不约束位置。"),
            QStringLiteral("垂直：两轴方向相差 90°，不约束位置。"),
            QStringLiteral("中心距离：两个模型原点之间的直线距离。"),
            QStringLiteral("原点重合：只约束两个原点位置，保留转动自由。"),
            QStringLiteral("指定夹角：有向轴之间的 0～180°，不约束位置。"),
            QStringLiteral("沿轴偏移：B−A 在 A "
                           "所选正向轴上的有符号投影；保留径向位置和姿态。与同轴组合可定位轴向位置。")};
        explanation->setText(help.value(int(t)));
        Scene candidate = document_.scene();
        QString why;
        if (!editId.isEmpty() && !findMate(candidate, editId))
            why = QStringLiteral("原配合已被移除，请关闭窗口重新选择。");
        bool replaced = false;
        for (auto &old : candidate.mates)
            if (old.id == m.id) {
                old = m;
                replaced = true;
                break;
            }
        if (!replaced)
            candidate.mates << m;
        bool valid = why.isEmpty() && candidate.validate(&why);
        bool solved = valid && (!preview->isChecked() || solve(candidate, &why));
        viewport_->setScene(candidate);
        viewport_->setMateSelection(m.id, solved ? (preview->isChecked()
                                                        ? QStringLiteral("预览 · 尚未应用")
                                                        : QStringLiteral("仅显示当前关系 · 尚未应用"))
                                                 : QStringLiteral("预览未满足 · 文档未修改"));
        buttons->button(QDialogButtonBox::Save)->setEnabled(valid);
        feedback->setStyleSheet(solved ? "color:#99dbbf;" : "color:#ffaaaa;");
        feedback->setText(
            !why.isEmpty()                           ? why
            : (state->pick == 1 || state->pick == 3) ? QStringLiteral("请在场景点击基准 A（Esc 取消点选）。")
            : state->pick == 2                       ? QStringLiteral("请在场景点击从动 B（Esc 取消点选）。")
                               : QStringLiteral("预览未写入文档；应用为一个撤销步骤，取消恢复当前文档。"));
    };
    for (auto *c : {source, target, type, sa, ta})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, [=](int) { update(); });
    for (auto *s : {distance, angle, offset})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dialog, [=](double) { update(); });
    connect(enabled, &QCheckBox::toggled, dialog, [=](bool) { update(); });
    connect(preview, &QCheckBox::toggled, dialog, [=](bool) { update(); });
    connect(pickPair, &QPushButton::clicked, dialog, [=] {
        state->pick = 1;
        update();
    });
    connect(pickA, &QPushButton::clicked, dialog, [=] {
        state->pick = 3;
        update();
    });
    connect(pickB, &QPushButton::clicked, dialog, [=] {
        state->pick = 2;
        update();
    });
    connect(swap, &QPushButton::clicked, dialog, [=] {
        const auto a = source->currentData(), b = target->currentData();
        const int axisA = sa->currentIndex(), axisB = ta->currentIndex();
        {
            QSignalBlocker x(source), y(target), z(sa), w(ta);
            source->setCurrentIndex(source->findData(b));
            target->setCurrentIndex(target->findData(a));
            sa->setCurrentIndex(axisB);
            ta->setCurrentIndex(axisA);
        }
        update();
    });
    connect(viewport_, &Viewport::selected, dialog, [=](const QString &id) {
        if (id.isEmpty() || !document_.scene().find(id))
            return;
        if (state->pick == 1 || state->pick == 3) {
            const bool pair = state->pick == 1;
            source->setCurrentIndex(source->findData(id));
            state->pick = pair ? 2 : 0;
        } else if (state->pick == 2) {
            if (id == source->currentData().toString()) {
                feedback->setText(QStringLiteral("A 与 B 不能是同一模型，请选择另一个模型。"));
                return;
            }
            target->setCurrentIndex(target->findData(id));
            state->pick = 0;
        }
        update();
    });
    connect(viewport_, &Viewport::matePickCancelled, dialog, [=] {
        state->pick = 0;
        update();
    });
    connect(&document_, &Document::changed, dialog, [=] {
        if (state->committing)
            return;
        const auto a = source->currentData(), b = target->currentData();
        {
            QSignalBlocker x(source), y(target);
            source->clear();
            target->clear();
            for (const auto &body : document_.scene().bodies) {
                source->addItem(body.name, body.id);
                target->addItem(body.name, body.id);
            }
            source->setCurrentIndex(source->findData(a));
            target->setCurrentIndex(target->findData(b));
        }
        update();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        const auto m = read();
        QString why;
        if (!editId.isEmpty() && !findMate(document_.scene(), editId)) {
            feedback->setText(QStringLiteral("原配合已被移除，请关闭并重新选择。"));
            return;
        }
        state->committing = true;
        bool ok = document_.edit(
            editId.isEmpty() ? QStringLiteral("添加 ") + mateName(m.type)
                             : QStringLiteral("编辑 ") + mateName(m.type),
            [&](Scene &scene) {
                bool replaced = false;
                for (auto &old : scene.mates)
                    if (old.id == m.id) {
                        old = m;
                        replaced = true;
                        break;
                    }
                if (!replaced)
                    scene.mates << m;
            },
            &why);
        state->committing = false;
        if (!ok) {
            feedback->setStyleSheet("color:#ffaaaa;");
            feedback->setText(why);
            return;
        }
        state->applied = true;
        log(QStringLiteral("配合已应用：") + mateName(m.type));
        dialog->accept();
    });
    connect(dialog, &QDialog::finished, this, [=](int) {
        mateEditor_.clear();
        viewport_->setMatePicking(false);
        viewport_->setScene(document_.scene());
        selectMate(state->applied ? state->draft.id : previousSelection);
    });
    viewport_->setMatePicking(true);
    dialog->show();
    dialog->move(mapToGlobal(QPoint(qMax(0, width() - dialog->width() - 15), 65)));
    update();
}
} // namespace af
