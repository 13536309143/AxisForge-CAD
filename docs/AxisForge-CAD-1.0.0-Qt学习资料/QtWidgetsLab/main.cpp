#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <QDebug>
#include <cmath>
#include <stdexcept>

// 纯 Qt 教学工程：列表中的对象没有三维几何，也没有撤销框架。
struct Item {
    QString id, name;
    double width = 30, height = 20;
};
class Window final : public QMainWindow {
public:
    Window() {
        setWindowTitle(QStringLiteral("Qt 入门：对象与属性"));
        resize(900, 600);
        list_ = new QListWidget(this);
        setCentralWidget(list_);
        dock_ = new QDockWidget(QStringLiteral("参数与属性"), this);
        dock_->setObjectName("propertiesDock");
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->addWidget(new QLabel(QStringLiteral("修改选中项，或输入参数创建新对象")));
        auto *form = new QFormLayout;
        name_ = new QLineEdit(QStringLiteral("对象"));
        width_ = new QDoubleSpinBox;
        height_ = new QDoubleSpinBox;
        for (auto *spin : {width_, height_}) {
            spin->setRange(0.1, 10000);
            spin->setDecimals(2);
            spin->setSuffix(" mm");
        }
        width_->setValue(30);
        height_->setValue(20);
        form->addRow(QStringLiteral("名称"), name_);
        form->addRow(QStringLiteral("宽度"), width_);
        form->addRow(QStringLiteral("高度"), height_);
        layout->addLayout(form);
        create_ = new QPushButton(QStringLiteral("创建新对象"));
        apply_ = new QPushButton(QStringLiteral("应用到选中对象"));
        layout->addWidget(create_);
        layout->addWidget(apply_);
        feedback_ = new QLabel;
        feedback_->setWordWrap(true);
        layout->addWidget(feedback_);
        layout->addStretch();
        dock_->setWidget(page);
        addDockWidget(Qt::RightDockWidgetArea, dock_);
        auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
        auto *save = fileMenu->addAction(QStringLiteral("保存 JSON"));
        auto *open = fileMenu->addAction(QStringLiteral("打开 JSON"));
        save->setShortcut(QKeySequence::Save);
        open->setShortcut(QKeySequence::Open);
        auto *view = menuBar()->addMenu(QStringLiteral("视图"));
        view->addAction(dock_->toggleViewAction());
        auto *edit = menuBar()->addMenu(QStringLiteral("编辑"));
        remove_ = edit->addAction(QStringLiteral("删除选中对象"));
        auto *toolbar = addToolBar(QStringLiteral("编辑"));
        toolbar->addAction(remove_);
        connect(create_, &QPushButton::clicked, this, [this] { createItem(); });
        connect(apply_, &QPushButton::clicked, this, [this] { applyItem(); });
        connect(list_, &QListWidget::currentRowChanged, this, [this](int) { showSelection(); });
        connect(remove_, &QAction::triggered, this, [this] {
            const int row = list_->currentRow();
            if (row < 0) return;
            items_.removeAt(row);
            refresh();
        });
        connect(save, &QAction::triggered, this, [this] {
            const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存"), {}, "JSON (*.json)");
            if (path.isEmpty()) return;
            QString why;
            if (!saveFile(path, why)) QMessageBox::warning(this, QStringLiteral("失败"), why);
        });
        connect(open, &QAction::triggered, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开"), {}, "JSON (*.json)");
            if (path.isEmpty()) return;
            QString why;
            if (!loadFile(path, why)) QMessageBox::warning(this, QStringLiteral("失败"), why);
        });
        refresh();
    }
    int selfTest() {
        int checks = 0;
        auto require = [&](bool ok, const char *message) {
            if (!ok) throw std::runtime_error(message);
            ++checks;
        };
        try {
            name_->setText(QStringLiteral("底座"));
            create_->click();
            require(items_.size() == 1 && list_->count() == 1, "create/list");
            require(items_[0].name == QStringLiteral("底座"), "name");
            require(apply_->isEnabled() && remove_->isEnabled(), "selection actions");
            width_->setValue(80);
            apply_->click();
            require(items_[0].width == 80, "apply");
            name_->clear();
            create_->click();
            require(items_.size() == 1 && !feedback_->text().isEmpty(), "empty name rejected");
            dock_->hide();
            require(!dock_->isVisible(), "hide");
            dock_->toggleViewAction()->trigger();
            require(dock_->isVisible(), "reopen");
            QTemporaryDir temporary;
            require(temporary.isValid(), "temporary directory");
            QString why;
            const QString file = temporary.filePath("scene.json");
            require(saveFile(file, why), "save");
            const QString id = items_[0].id;
            items_.clear(); refresh();
            require(loadFile(file, why), "load");
            require(items_.size() == 1 && items_[0].id == id && items_[0].width == 80, "round trip");
            QFile bad(temporary.filePath("bad.json"));
            require(bad.open(QIODevice::WriteOnly), "bad file create");
            bad.write("{\"items\":[{\"id\":\"x\",\"name\":\"x\",\"width\":-1,\"height\":20}]}");
            bad.close();
            require(!loadFile(bad.fileName(), why) && items_.size() == 1 && items_[0].id == id, "invalid load rollback");
            remove_->trigger();
            require(items_.isEmpty() && list_->count() == 0 && !apply_->isEnabled(), "delete");
            qInfo() << "QtWidgetsLab:" << checks << "checks passed";
            return 0;
        } catch (const std::exception &error) {
            qCritical() << error.what();
            return 1;
        }
    }
private:
    void createItem() {
        const QString name = name_->text().trimmed();
        if (name.isEmpty()) { feedback_->setText(QStringLiteral("请输入名称")); return; }
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        items_.push_back({id, name, width_->value(), height_->value()});
        feedback_->clear();
        refresh(id);
    }
    void applyItem() {
        const int row = list_->currentRow();
        if (row < 0) return;
        if (name_->text().trimmed().isEmpty()) { feedback_->setText(QStringLiteral("请输入名称")); return; }
        auto &item = items_[row];
        item.name = name_->text().trimmed();
        item.width = width_->value(); item.height = height_->value();
        const QString id = item.id;
        feedback_->clear(); refresh(id);
    }
    void showSelection() {
        const int row = list_->currentRow();
        const bool valid = row >= 0 && row < items_.size();
        apply_->setEnabled(valid); remove_->setEnabled(valid);
        if (!valid) return;
        const auto &item = items_[row];
        name_->setText(item.name); width_->setValue(item.width); height_->setValue(item.height);
    }
    void refresh(const QString &selected = {}) {
        list_->clear();
        int selectedRow = -1;
        for (int i = 0; i < items_.size(); ++i) {
            const auto &item = items_[i];
            auto *row = new QListWidgetItem(QString("%1  |  %2 x %3 mm").arg(item.name).arg(item.width).arg(item.height), list_);
            row->setData(Qt::UserRole, item.id);
            if (item.id == selected) selectedRow = i;
        }
        if (selectedRow < 0 && !items_.isEmpty()) selectedRow = 0;
        list_->setCurrentRow(selectedRow);
        showSelection();
        statusBar()->showMessage(QStringLiteral("%1 个对象（教学数据，无三维几何）").arg(items_.size()));
    }
    bool saveFile(const QString &path, QString &why) const {
        QJsonArray array;
        for (const auto &item : items_) array.append(QJsonObject{{"id",item.id},{"name",item.name},{"width",item.width},{"height",item.height}});
        const auto bytes = QJsonDocument(QJsonObject{{"items",array}}).toJson();
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            why = output.errorString(); return false;
        }
        return true;
    }
    bool loadFile(const QString &path, QString &why) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) { why = input.errorString(); return false; }
        if (input.size() > 1024 * 1024) { why = QStringLiteral("练习文件最多 1 MiB"); return false; }
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(input.readAll(), &error);
        auto fail = [&] { why = QStringLiteral("JSON 结构、名称、ID 或尺寸无效"); return false; };
        if (error.error != QJsonParseError::NoError || !json.isObject() || !json.object()["items"].isArray()) return fail();
        const auto array = json.object()["items"].toArray();
        if (array.size() > 300) return fail();
        QVector<Item> candidate;
        for (const auto &value : array) {
            if (!value.isObject()) return fail();
            const auto object = value.toObject();
            if (!object["id"].isString() || !object["name"].isString() || !object["width"].isDouble() || !object["height"].isDouble()) return fail();
            Item item{object["id"].toString(), object["name"].toString().trimmed(), object["width"].toDouble(), object["height"].toDouble()};
            if (item.id.isEmpty() || item.name.isEmpty() || !std::isfinite(item.width) || !std::isfinite(item.height) || item.width < .1 || item.width > 10000 || item.height < .1 || item.height > 10000) return fail();
            for (const auto &existing : candidate) if (existing.id == item.id) return fail();
            candidate.push_back(item);
        }
        items_ = candidate; refresh(); return true;
    }
    QVector<Item> items_;
    QListWidget *list_ = nullptr;
    QDockWidget *dock_ = nullptr;
    QLineEdit *name_ = nullptr;
    QDoubleSpinBox *width_ = nullptr, *height_ = nullptr;
    QPushButton *create_ = nullptr, *apply_ = nullptr;
    QAction *remove_ = nullptr;
    QLabel *feedback_ = nullptr;
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Window window;
    window.show();
    if (app.arguments().contains("--self-test"))
        QTimer::singleShot(200, &window, [&] { app.exit(window.selfTest()); });
    return app.exec();
}
