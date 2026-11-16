#include "MainWindow.h"
#include "core/ParameterFields.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
namespace af {
void MainWindow::createBodyDialog(Shape shape) {
    const auto fields = parameterFields(shape);
    if (fields.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setObjectName("createBodyDialog");
    dialog.setWindowTitle(QStringLiteral("创建") + shapeName(shape));
    dialog.setMinimumWidth(380);
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(shapeName(shape) + QStringLiteral(" · 建模参数"));
    title->setStyleSheet("font-size:18px;font-weight:bold;color:#8bd9d0;");
    layout->addWidget(title);
    auto *hint = new QLabel(QStringLiteral("长度单位：mm。确认参数后创建模型，取消不会修改场景。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(shapeName(shape) + QString(" %1").arg(document_.scene().bodies.size() + 1));
    name->setMaxLength(160);
    name->setObjectName("createName");
    form->addRow(QStringLiteral("名称"), name);
    Parameters parameters;
    QMap<QString, QDoubleSpinBox *> inputs;
    for (const auto &field : fields) {
        auto *spin = new QDoubleSpinBox;
        spin->setObjectName("create_" + field.key);
        spin->setRange(.1, 10000);
        spin->setDecimals(3);
        spin->setSuffix(" mm");
        spin->setValue(parameters.*(field.member));
        inputs[field.key] = spin;
        form->addRow(field.label, spin);
    }
    QSpinBox *segments = nullptr;
    if (hasSubdivision(shape)) {
        segments = new QSpinBox;
        segments->setObjectName("create_segments");
        segments->setRange(8, 128);
        segments->setValue(parameters.segments);
        form->addRow(QStringLiteral("曲面细分"), segments);
    }
    layout->addLayout(form);
    if (shape == Shape::Torus || shape == Shape::Tube || shape == Shape::Frustum) {
        auto *rule = new QLabel(shape == Shape::Torus
                                    ? QStringLiteral("次半径必须小于主半径。主半径为环中心到管中心的距离。")
                                : shape == Shape::Tube ? QStringLiteral("内半径必须小于外半径。")
                                                       : QStringLiteral("顶半径必须小于底半径。"));
        rule->setWordWrap(true);
        layout->addWidget(rule);
    }
    auto *feedback = new QLabel;
    feedback->setObjectName("createFeedback");
    feedback->setWordWrap(true);
    feedback->setStyleSheet("color:#ffaaaa;");
    layout->addWidget(feedback);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建模型"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("createBodyConfirm");
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty()) {
            feedback->setText(QStringLiteral("请输入模型名称。"));
            return;
        }
        for (const auto &field : fields)
            parameters.*(field.member) = inputs[field.key]->value();
        if (segments)
            parameters.segments = segments->value();
        QString why;
        const auto id = document_.addBody(shape, parameters, name->text().trimmed(), &why);
        if (id.isEmpty()) {
            feedback->setText(why);
            return;
        }
        selectBody(id);
        viewport_->fitAll();
        log(QStringLiteral("创建：") + name->text().trimmed());
        dialog.accept();
    });
    dialog.exec();
}
} // namespace af
