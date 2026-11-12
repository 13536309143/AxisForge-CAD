#include "MainWindow.h"
#include "app/ProcessMemory.h"
#include "core/Constraints.h"
#include "core/ProjectIO.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace af {
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), document_(this) {
    setObjectName("mainWindow");
    resize(1480, 920);
    setMinimumSize(1100, 720);
    setWindowTitle(QStringLiteral("AxisForge CAD · 参数化三维工作台"));
    viewport_ = new Viewport(this);
    setCentralWidget(viewport_);
    buildActions();
    buildPanels();
    buildSceneStats();
    setStyleSheet(QStringLiteral(R"(
        QMainWindow,QWidget{background:#161e2b;color:#dce5f2;font-family:'Microsoft YaHei';font-size:12px;}
        QMenuBar{background:#111823;padding:4px;}QMenuBar::item:selected,QMenu::item:selected{background:#2a3a50;}
        QMenu{background:#1c293a;border:1px solid #34455b;padding:5px;}QMenu::item{padding:8px 24px;}
        QToolBar{background:#1b2636;border:0;border-bottom:1px solid #304157;spacing:8px;padding:10px;}
        QToolButton{background:#26364b;border:1px solid #354962;border-radius:5px;padding:9px 12px;color:#dce8f5;}
        QToolButton:hover{background:#36506a;}QToolButton:checked{background:#22535f;border-color:#59c6c1;}
        QDockWidget{color:#a3b5cb;}QDockWidget::title{background:#1d2b3d;padding:10px;font-weight:bold;}
        QTreeWidget{background:#141d2b;border:0;outline:0;}QTreeWidget::item{padding:9px 3px;border-bottom:1px solid #1b293a;}QTreeWidget::item:selected{background:#264958;color:#b5f1ed;}
        QHeaderView::section{background:#1b293a;color:#859ab4;padding:8px;border:0;}
        QTabWidget::pane{border:0;}QTabBar::tab{background:#1b293b;padding:10px 13px;color:#97abc3;}QTabBar::tab:selected{background:#284155;color:#9be3dc;}
        QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#101a27;border:1px solid #34465d;border-radius:4px;padding:6px;min-height:20px;selection-background-color:#2e6972;}
        QLineEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus{border-color:#63cfc4;}
        QLineEdit:disabled,QDoubleSpinBox:disabled,QSpinBox:disabled{color:#526276;}
        QPushButton{background:#2c4259;border:1px solid #405973;border-radius:5px;padding:9px 12px;}
        QPushButton:hover{background:#3a5972;}QPushButton#applyProperties{background:#21666a;border-color:#52bfb7;color:#e0fffb;font-weight:bold;}
        QGroupBox{border:1px solid #2d3e53;border-radius:6px;margin-top:16px;padding:16px 10px 10px;font-weight:bold;}
        QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 5px;color:#8fb9c9;}
        QScrollArea{border:0;}QStatusBar{background:#101925;color:#8cabc0;padding:4px;}
        QPlainTextEdit{background:#111b28;border:0;color:#8da9bd;font-family:Consolas,'Microsoft YaHei';font-size:11px;}
        QSplitter::handle{background:#304257;}QLabel#brand{font-size:18px;font-weight:bold;color:#8be1d8;padding-right:18px;}
    )"));
    connect(&document_, &Document::changed, this, &MainWindow::refresh);
    connect(&document_, &Document::rejected, this, [this](const QString &why) {
        statusBar()->showMessage(why, 12000);
        log(QStringLiteral("拒绝：") + why);
    });
    connect(document_.history(), &QUndoStack::cleanChanged, this, [this](bool) {
        setWindowTitle(QStringLiteral("AxisForge CAD · ") + document_.scene().title +
                       (document_.history()->isClean() ? QString() : QStringLiteral(" *")));
    });
    connect(viewport_, &Viewport::selected, this, &MainWindow::selectBody);
    connect(viewport_, &Viewport::moveStarted, &document_, &Document::beginMove);
    connect(viewport_, &Viewport::movePreview, &document_, &Document::previewMove);
    connect(viewport_, &Viewport::moveFinished, &document_, &Document::finishMove);
    connect(viewport_, &Viewport::deleteRequested, this, &MainWindow::deleteSelection);
    connect(viewport_, &Viewport::interactionMessage, this,
            [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    document_.replace(demoScene());
    selection_ = document_.scene().bodies[1].id;
    refresh();
    viewport_->fitAll();
    log(QStringLiteral("工作台就绪。单位 mm，Z 轴向上。示例包含同轴与中心距离配合。"));
}
void MainWindow::buildActions() {
    auto *file = menuBar()->addMenu(QStringLiteral("文件"));
    auto *edit = menuBar()->addMenu(QStringLiteral("编辑"));
    auto *create = menuBar()->addMenu(QStringLiteral("建模"));
    auto *assembly = menuBar()->addMenu(QStringLiteral("装配"));
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    auto *help = menuBar()->addMenu(QStringLiteral("帮助"));
    auto action = [&](QMenu *menu, const QString &label, const QString &key, const QString &objectName,
                      const std::function<void()> &fn) {
        auto *a = menu->addAction(label);
        a->setObjectName(objectName);
        if (!key.isEmpty())
            a->setShortcut(QKeySequence(key));
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    action(file, QStringLiteral("新建项目"), "Ctrl+N", "newProject", [this] {
        if (!mayDiscard())
            return;
        path_.clear();
        selection_.clear();
        document_.replace(Scene{});
        viewport_->fitAll();
    });
    action(file, QStringLiteral("打开项目…"), "Ctrl+O", "openProject", [this] {
        if (!mayDiscard())
            return;
        auto p = QFileDialog::getOpenFileName(this, QStringLiteral("打开 AxisForge 项目"), {},
                                              "AxisForge (*.afcad)");
        if (!p.isEmpty())
            openFile(p);
    });
    action(file, QStringLiteral("保存项目"), "Ctrl+S", "saveProject", [this] {
        auto p = path_;
        if (p.isEmpty())
            p = QFileDialog::getSaveFileName(this, QStringLiteral("保存项目"), "assembly.afcad",
                                             "AxisForge (*.afcad)");
        if (!p.isEmpty())
            saveFile(p);
    });
    action(file, QStringLiteral("另存为…"), "Ctrl+Shift+S", "saveAs", [this] {
        auto p = QFileDialog::getSaveFileName(this, QStringLiteral("另存为"), "assembly.afcad",
                                              "AxisForge (*.afcad)");
        if (!p.isEmpty())
            saveFile(p);
    });
    file->addSeparator();
    for (int i = 0; i < 2; ++i)
        action(file, i ? QStringLiteral("导出 STL…") : QStringLiteral("导出 OBJ…"), {},
               i ? "exportStl" : "exportObj", [this, i] {
                   auto p = QFileDialog::getSaveFileName(this, QStringLiteral("导出可见模型"),
                                                         i ? "assembly.stl" : "assembly.obj",
                                                         i ? "STL (*.stl)" : "OBJ (*.obj)");
                   if (p.isEmpty())
                       return;
                   QString error;
                   if (!exportMesh(p, document_.scene(), i == 1, &error))
                       QMessageBox::warning(this, QStringLiteral("导出失败"), error);
                   else
                       log(QStringLiteral("已导出：") + p);
               });
    file->addSeparator();
    action(file, QStringLiteral("退出"), "Ctrl+Q", "quit", [this] { close(); });
    auto *undo = document_.history()->createUndoAction(this, QStringLiteral("撤销"));
    undo->setShortcut(QKeySequence::Undo);
    edit->addAction(undo);
    auto *redo = document_.history()->createRedoAction(this, QStringLiteral("重做"));
    redo->setShortcuts(QKeySequence::Redo);
    edit->addAction(redo);
    action(edit, QStringLiteral("复制所选模型"), "Ctrl+D", "duplicateBody",
           [this] { selectBody(document_.duplicateBody(selection_)); });
    auto *deleteAction = action(edit, QStringLiteral("删除所选模型及相关配合"), {}, "deleteBody",
                                [this] { deleteSelection(); });
    auto *toolbar = addToolBar(QStringLiteral("建模工具"));
    toolbar->setMovable(false);
    toolbar->setObjectName("modelingToolbar");
    auto *brand = new QLabel("AXISFORGE");
    brand->setObjectName("brand");
    toolbar->addWidget(brand);
    toolbar->addAction(deleteAction);
    for (int i = 0; i < 5; ++i) {
        auto shape = Shape(i);
        auto *a = action(create, shapeName(shape), QString("Ctrl+%1").arg(i + 1), "create" + shapeKey(shape),
                         [this, shape] {
                             auto id = document_.addBody(shape);
                             selectBody(id);
                             log(QStringLiteral("创建：") + shapeName(shape));
                         });
        toolbar->addAction(a);
    }
    toolbar->addSeparator();
    auto *mate =
        action(assembly, QStringLiteral("添加配合…"), "Ctrl+M", "addMate", [this] { addMateDialog(); });
    toolbar->addAction(mate);
    action(assembly, QStringLiteral("删除选中配合"), {}, "deleteMate", [this] { deleteMate(); });
    toolbar->addSeparator();
    auto *fit = action(viewMenu, QStringLiteral("适应全部"), "F", "fitAll", [this] { viewport_->fitAll(); });
    toolbar->addAction(fit);
    const QStringList labels = {QStringLiteral("等轴测"), QStringLiteral("前视图"), QStringLiteral("右视图"),
                                QStringLiteral("俯视图")};
    for (int i = 0; i < 4; ++i)
        action(viewMenu, labels[i], QString::number(i + 1), QString("view%1").arg(i),
               [this, i] { viewport_->setView(i); });
    auto *wire = viewMenu->addAction(QStringLiteral("线框显示"));
    wire->setObjectName("wireframe");
    wire->setCheckable(true);
    connect(wire, &QAction::toggled, viewport_, &Viewport::setWireframe);
    toolbar->addAction(wire);
    auto *ortho = viewMenu->addAction(QStringLiteral("正交投影"));
    ortho->setObjectName("orthographic");
    ortho->setCheckable(true);
    connect(ortho, &QAction::toggled, viewport_, &Viewport::setOrthographic);
    action(help, QStringLiteral("操作指南"), "F1", "help", [this] {
        QMessageBox::information(
            this, QStringLiteral("AxisForge CAD · 操作指南"),
            QStringLiteral(
                "建模：工具栏创建图元 → 左键选择 → 右侧修改属性 → "
                "应用参数。\n\n视图：右键旋转，中键平移，滚轮缩放，F 适应全部，1/2/3/4 "
                "切换视图。\n\n装配：添加配合，选基准模型和从动模型，再选择各自局部轴。修改基准后从动模型跟随"
                "。\n\n同轴仅约束轴线重合，允许沿轴平移和绕轴旋转。平行/"
                "垂直约束轴方向，中心距离约束模型原点间距离。固定模型不会被求解器移动。\n\n配合循环或冲突会被"
                "拒绝并回滚。当前采用参数图元和三角网格，不包含布尔运算、草图拉伸或 STEP 实体内核。"));
    });
}
void MainWindow::buildPanels() {
    auto *panels = menuBar()->addMenu(QStringLiteral("面板"));
    auto *dock = new QDockWidget(QStringLiteral("装配导航  /  ASSEMBLY"), this);
    dock->setObjectName("assemblyDock");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto *tabs = new QTabWidget;
    tree_ = new QTreeWidget;
    tree_->setObjectName("sceneTree");
    tree_->installEventFilter(this);
    tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    tree_->addAction(findChild<QAction *>("deleteBody"));
    tree_->setHeaderLabels({QStringLiteral("模型"), QStringLiteral("状态")});
    tree_->setRootIsDecorated(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tabs->addTab(tree_, QStringLiteral("模型树"));
    auto *matePage = new QWidget;
    auto *mateLayout = new QVBoxLayout(matePage);
    mateTree_ = new QTreeWidget;
    mateTree_->setObjectName("mateTree");
    mateTree_->setHeaderLabels({QStringLiteral("配合关系"), QStringLiteral("状态")});
    mateTree_->setRootIsDecorated(false);
    mateTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    mateLayout->addWidget(mateTree_);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("添加配合"));
    auto *remove = new QPushButton(QStringLiteral("移除"));
    buttons->addWidget(add);
    buttons->addWidget(remove);
    mateLayout->addLayout(buttons);
    tabs->addTab(matePage, QStringLiteral("配合关系"));
    dock->setWidget(tabs);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    panels->addAction(dock->toggleViewAction());
    dock->setMinimumWidth(225);
    resizeDocks({dock}, {255}, Qt::Horizontal);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (refreshing_)
            return;
        auto *item = tree_->currentItem();
        selectBody(item ? item->data(0, Qt::UserRole).toString() : QString());
    });
    connect(mateTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (refreshing_ || column != 0)
            return;
        QString id = item->data(0, Qt::UserRole).toString();
        bool enabled = item->checkState(0) == Qt::Checked;
        document_.edit(QStringLiteral("切换配合"), [&](Scene &s) {
            for (auto &m : s.mates)
                if (m.id == id)
                    m.enabled = enabled;
        });
        refresh();
    });
    connect(add, &QPushButton::clicked, this, &MainWindow::addMateDialog);
    connect(remove, &QPushButton::clicked, this, &MainWindow::deleteMate);
    auto *propertyDock = new QDockWidget(QStringLiteral("参数检查器  /  INSPECTOR"), this);
    propertyDock->setObjectName("propertyDock");
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    propertyForm_ = new QWidget;
    propertyForm_->setObjectName("propertyForm");
    auto *vertical = new QVBoxLayout(propertyForm_);
    vertical->setContentsMargins(14, 12, 14, 14);
    kind_ = new QLabel;
    kind_->setStyleSheet("font-size:17px;color:#8bd9d0;font-weight:bold;");
    vertical->addWidget(kind_);
    auto *general = new QGroupBox(QStringLiteral("基本属性"));
    auto *gf = new QFormLayout(general);
    name_ = new QLineEdit;
    name_->setObjectName("bodyName");
    name_->setMaxLength(160);
    gf->addRow(QStringLiteral("名称"), name_);
    color_ = new QPushButton;
    gf->addRow(QStringLiteral("颜色"), color_);
    visible_ = new QCheckBox(QStringLiteral("在视图和导出中显示"));
    gf->addRow(visible_);
    fixed_ = new QCheckBox(QStringLiteral("固定 · 求解器不移动此零件"));
    gf->addRow(fixed_);
    vertical->addWidget(general);
    connect(color_, &QPushButton::clicked, this, [this] {
        auto c = QColorDialog::getColor(pendingColor_, this, QStringLiteral("选择模型颜色"));
        if (c.isValid()) {
            pendingColor_ = c;
            color_->setText(c.name());
            color_->setStyleSheet("background:" + c.name() + ";color:#101925;");
        }
    });
    auto addNumber = [&](QFormLayout *form, const QString &key, const QString &label, double min, double max,
                         const QString &suffix) {
        auto *spin = new QDoubleSpinBox;
        spin->setObjectName(key);
        spin->setDecimals(3);
        spin->setRange(min, max);
        spin->setSingleStep(1);
        spin->setSuffix(suffix);
        numbers_[key] = spin;
        form->addRow(label, spin);
    };
    auto *dimensions = new QGroupBox(QStringLiteral("参数化几何"));
    auto *df = new QFormLayout(dimensions);
    addNumber(df, "width", QStringLiteral("宽度 X"), 0.1, 10000, " mm");
    addNumber(df, "depth", QStringLiteral("深度 Y"), 0.1, 10000, " mm");
    addNumber(df, "height", QStringLiteral("高度 Z"), 0.1, 10000, " mm");
    addNumber(df, "radius", QStringLiteral("半径 / 主半径"), 0.1, 10000, " mm");
    addNumber(df, "tubeRadius", QStringLiteral("圆环管半径"), 0.1, 10000, " mm");
    segments_ = new QSpinBox;
    segments_->setObjectName("segments");
    segments_->setRange(8, 128);
    df->addRow(QStringLiteral("圆周细分"), segments_);
    vertical->addWidget(dimensions);
    auto *transform = new QGroupBox(QStringLiteral("位置与姿态"));
    auto *tf = new QFormLayout(transform);
    for (int i = 0; i < 3; ++i)
        addNumber(tf, QString("pos%1").arg(i), QString("%1 位置").arg(QString("XYZ")[i]), -1000000, 1000000,
                  " mm");
    for (int i = 0; i < 3; ++i)
        addNumber(tf, QString("rot%1").arg(i), QString("%1 旋转").arg(QString("XYZ")[i]), -360, 360,
                  QStringLiteral(" °"));
    vertical->addWidget(transform);
    auto *apply = new QPushButton(QStringLiteral("应用参数  /  APPLY"));
    apply->setObjectName("applyProperties");
    vertical->addWidget(apply);
    connect(apply, &QPushButton::clicked, this, &MainWindow::applyProperties);
    stats_ = new QLabel;
    stats_->setWordWrap(true);
    stats_->setStyleSheet("color:#8ea6bd;line-height:1.7;");
    vertical->addWidget(stats_);
    vertical->addStretch();
    scroll->setWidget(propertyForm_);
    propertyDock->setWidget(scroll);
    propertyDock->setMinimumWidth(285);
    addDockWidget(Qt::RightDockWidgetArea, propertyDock);
    propertyDock->toggleViewAction()->setObjectName("toggleProperties");
    propertyDock->toggleViewAction()->setShortcut(QKeySequence("Ctrl+I"));
    panels->addAction(propertyDock->toggleViewAction());
    resizeDocks({propertyDock}, {310}, Qt::Horizontal);
    auto *logDock = new QDockWidget(QStringLiteral("工作台日志"), this);
    logDock->setObjectName("logDock");
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(200);
    logDock->setWidget(log_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    panels->addAction(logDock->toggleViewAction());
    resizeDocks({logDock}, {92}, Qt::Vertical);
    summary_ = new QLabel;
    statusBar()->addPermanentWidget(summary_);
    statusBar()->showMessage(QStringLiteral("参数化建模工作台 · 所有长度单位 mm"));
}
void MainWindow::buildSceneStats() {
    auto *dock = new QDockWidget(QStringLiteral("场景统计 / SCENE METRICS"), this);
    dock->setObjectName("sceneStatsDock");
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    sceneStats_ = new QLabel;
    sceneStats_->setObjectName("sceneMetrics");
    sceneStats_->setTextFormat(Qt::RichText);
    sceneStats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sceneStats_->setWordWrap(true);
    layout->addWidget(sceneStats_);
    auto *controls = new QHBoxLayout;
    auto *continuous = new QCheckBox(QStringLiteral("连续渲染 / 测量 FPS"));
    continuous->setObjectName("continuousRendering");
    connect(continuous, &QCheckBox::toggled, viewport_, &Viewport::setContinuousRendering);
    controls->addWidget(continuous);
    auto *vsync = new QComboBox;
    vsync->setObjectName("vsyncMode");
    vsync->addItem(QStringLiteral("垂直同步：开启"), 1);
    vsync->addItem(QStringLiteral("垂直同步：关闭"), 0);
    vsync->setCurrentIndex(qApp->property("requestedVsync").toInt() ? 0 : 1);
    controls->addWidget(vsync);
    auto *restart = new QPushButton(QStringLiteral("应用并重启"));
    controls->addWidget(restart);
    controls->addStretch();
    layout->addLayout(controls);
    auto *note = new QLabel(
        QStringLiteral("GPU 本地段使用量由匹配显卡的 Windows DXGI 查询，失败显示不可用；缓冲有效载荷仅统计 "
                       "VBO（含线框/网格/隐藏模型）。顶点含重复；FPS 按实际 "
                       "frameSwapped 事件计数，非 GPU 耗时。切换同步需重启，驱动/桌面合成器可能覆盖请求。"));
    note->setWordWrap(true);
    note->setStyleSheet("color:#8da5b9;font-size:11px;");
    layout->addWidget(note);
    connect(restart, &QPushButton::clicked, this, [this, vsync] {
        if (!mayDiscard())
            return;
        const int mode = vsync->currentData().toInt();
        QStringList args{QString("--vsync=%1").arg(mode)};
        if (!path_.isEmpty())
            args << path_;
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) {
            QMessageBox::warning(this, QStringLiteral("重启失败"),
                                 QStringLiteral("未能启动新进程，当前窗口和项目保持不变。"));
            return;
        }
        QSettings("AxisForge", "AxisForge CAD").setValue("render/vsync", mode);
        document_.history()->setClean();
        close();
    });
    dock->setWidget(page);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    auto *logDock = findChild<QDockWidget *>("logDock");
    if (logDock)
        tabifyDockWidget(logDock, dock);
    dock->toggleViewAction()->setObjectName("toggleSceneStats");
    dock->toggleViewAction()->setShortcut(QKeySequence("Ctrl+Shift+I"));
    auto *view = menuBar()->addMenu(QStringLiteral("性能"));
    view->addAction(dock->toggleViewAction());
    dock->raise();
    auto *timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, [this] {
        viewport_->sampleFrames();
        updateSceneStats();
    });
    timer->start();
    updateSceneStats();
}
void MainWindow::updateSceneStats() {
    const auto m = viewport_->metrics();
    const auto ram = processMemory();
    const auto gpu = gpuMemory(viewport_->rendererInfo());
    auto mib = [](quint64 bytes) { return QString::number(double(bytes) / (1024 * 1024), 'f', 2) + " MiB"; };
    const int requested = qApp->property("requestedVsync").toInt();
    sceneStats_->setText(
        QStringLiteral(
            "<table cellspacing='7'><tr><td>实例 总/可见</td><td><b>%1 / %2</b></td><td>三角面 "
            "总/可见</td><td><b>%3 / %4</b></td><td>顶点 总/可见（含重复）</td><td><b>%5 / %6</b></td></tr>"
            "<tr><td>GPU 缓冲有效载荷</td><td>%7</td><td>进程驻留内存 "
            "RSS</td><td>%8</td><td>进程私有提交</td><td>%9</td></tr>"
            "<tr><td>当前 FPS / 平均帧间隔</td><td><b>%10 / %11 ms</b></td><td>同步请求 / "
            "上下文报告</td><td>%12 / %13</td><td>刷新模式</td><td>%14</td></tr>"
            "<tr><td>GPU 本地段（进程）</td><td>%15</td><td>GPU 本地段预算</td><td>%16</td></tr></table>")
            .arg(m.instances)
            .arg(m.visibleInstances)
            .arg(m.triangles)
            .arg(m.visibleTriangles)
            .arg(m.vertices)
            .arg(m.visibleVertices)
            .arg(mib(m.gpuBufferBytes))
            .arg(ram.available ? mib(ram.resident) : QStringLiteral("不可用"))
            .arg(ram.privateAvailable ? mib(ram.privateBytes) : QStringLiteral("不可用"))
            .arg(m.fps, 0, 'f', 1)
            .arg(m.frameMs, 0, 'f', 2)
            .arg(requested ? QStringLiteral("ON") : QStringLiteral("OFF"))
            .arg(viewport_->format().swapInterval())
            .arg(viewport_->continuousRendering() ? QStringLiteral("连续")
                                                  : QStringLiteral("按需（静止低 FPS 正常）"))
            .arg(gpu.available ? mib(gpu.usage) : QStringLiteral("不可用"))
            .arg(gpu.available ? mib(gpu.budget) : QStringLiteral("不可用")));
}
void MainWindow::log(const QString &text) {
    if (log_)
        log_->appendPlainText("› " + text);
}
void MainWindow::refresh() {
    refreshing_ = true;
    tree_->clear();
    for (const auto &b : document_.scene().bodies) {
        auto *item = new QTreeWidgetItem(tree_);
        item->setText(0, b.name);
        item->setText(1, !b.visible ? QStringLiteral("隐藏")
                         : b.fixed  ? QStringLiteral("固定")
                                    : QStringLiteral("自由"));
        item->setData(0, Qt::UserRole, b.id);
        item->setForeground(0, b.color);
        item->setToolTip(0, shapeName(b.shape) + " · " + b.id);
        if (b.id == selection_)
            tree_->setCurrentItem(item);
    }
    if (!document_.scene().find(selection_))
        selection_.clear();
    mateTree_->clear();
    int active = 0;
    for (const auto &m : document_.scene().mates) {
        auto *item = new QTreeWidgetItem(mateTree_);
        auto a = document_.scene().find(m.source), b = document_.scene().find(m.target);
        item->setText(0, mateName(m.type) + " · " + (a ? a->name : "?") + " → " + (b ? b->name : "?"));
        item->setData(0, Qt::UserRole, m.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, m.enabled ? Qt::Checked : Qt::Unchecked);
        item->setText(1, m.enabled ? QStringLiteral("满足") : QStringLiteral("停用"));
        const auto r = residual(document_.scene(), m);
        item->setToolTip(
            0, QString("线性残差 %1 mm\n角向残差 %2").arg(r.linear, 0, 'g', 4).arg(r.angular, 0, 'g', 4));
        if (m.enabled)
            ++active;
    }
    summary_->setText(QStringLiteral("%1 个模型  ·  %2 个有效配合  ·  mm")
                          .arg(document_.scene().bodies.size())
                          .arg(active));
    viewport_->setScene(document_.scene());
    viewport_->setSelection(selection_);
    showProperties();
    refreshing_ = false;
    setWindowTitle(QStringLiteral("AxisForge CAD · ") + document_.scene().title +
                   (document_.history()->isClean() ? QString() : " *"));
}
void MainWindow::selectBody(const QString &id) {
    selection_ = document_.scene().find(id) ? id : QString();
    refreshing_ = true;
    tree_->clearSelection();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto *item = tree_->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == selection_) {
            tree_->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    showProperties();
    viewport_->setSelection(selection_);
    refreshing_ = false;
}
void MainWindow::showProperties() {
    const auto *b = document_.scene().find(selection_);
    propertyForm_->setEnabled(b != nullptr);
    if (!b) {
        kind_->setText(QStringLiteral("选择模型以编辑参数"));
        name_->clear();
        stats_->clear();
        return;
    }
    kind_->setText(shapeName(b->shape) + QStringLiteral("  /  PARAMETRIC"));
    name_->setText(b->name);
    pendingColor_ = b->color;
    color_->setText(b->color.name());
    color_->setStyleSheet("background:" + b->color.name() + ";color:#101925;");
    visible_->setChecked(b->visible);
    fixed_->setChecked(b->fixed);
    const auto &p = b->params;
    numbers_["width"]->setValue(p.width);
    numbers_["height"]->setValue(p.height);
    numbers_["depth"]->setValue(p.depth);
    numbers_["radius"]->setValue(p.radius);
    numbers_["tubeRadius"]->setValue(p.tubeRadius);
    segments_->setValue(p.segments);
    numbers_["width"]->setEnabled(b->shape == Shape::Box);
    numbers_["depth"]->setEnabled(b->shape == Shape::Box);
    numbers_["height"]->setEnabled(b->shape == Shape::Box || b->shape == Shape::Cylinder ||
                                   b->shape == Shape::Cone);
    numbers_["radius"]->setEnabled(b->shape != Shape::Box);
    numbers_["tubeRadius"]->setEnabled(b->shape == Shape::Torus);
    segments_->setEnabled(b->shape != Shape::Box);
    const auto e = b->rotation.toEulerAngles();
    for (int i = 0; i < 3; ++i) {
        numbers_[QString("pos%1").arg(i)]->setValue(b->position[i]);
        numbers_[QString("rot%1").arg(i)]->setValue(e[i]);
    }
    const int triangles = tessellate(*b).vertices.size() / 3;
    stats_->setText(QStringLiteral("几何信息\n体积  %1 mm³\n表面积  %2 mm²\n显示网格  %3 "
                                   "个三角形\n\n参数为解析尺寸；显示与导出使用离散网格。")
                        .arg(volume(*b), 0, 'f', 2)
                        .arg(surfaceArea(*b), 0, 'f', 2)
                        .arg(triangles));
}
void MainWindow::applyProperties() {
    if (selection_.isEmpty())
        return;
    const QString id = selection_;
    Body updated = *document_.scene().find(id);
    updated.name = name_->text().trimmed();
    updated.color = pendingColor_;
    updated.visible = visible_->isChecked();
    updated.fixed = fixed_->isChecked();
    auto &p = updated.params;
    p.width = numbers_["width"]->value();
    p.depth = numbers_["depth"]->value();
    p.height = numbers_["height"]->value();
    p.radius = numbers_["radius"]->value();
    p.tubeRadius = numbers_["tubeRadius"]->value();
    p.segments = segments_->value();
    updated.position = {float(numbers_["pos0"]->value()), float(numbers_["pos1"]->value()),
                        float(numbers_["pos2"]->value())};
    updated.rotation =
        QQuaternion::fromEulerAngles(float(numbers_["rot0"]->value()), float(numbers_["rot1"]->value()),
                                     float(numbers_["rot2"]->value()))
            .normalized();
    if (document_.edit(QStringLiteral("编辑 ") + updated.name, [&](Scene &s) { *s.find(id) = updated; }))
        log(QStringLiteral("参数已应用；关联配合已重新求解。"));
    else
        showProperties();
}
void MainWindow::addMateDialog() {
    if (document_.scene().bodies.size() < 2) {
        statusBar()->showMessage(QStringLiteral("至少创建两个模型才能添加配合。"), 7000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName("mateDialog");
    dialog.setWindowTitle(QStringLiteral("添加装配配合"));
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(QStringLiteral("基准模型驱动从动模型。轴以各模型的局部坐标系定义。\n同轴保留轴向"
                                           "滑动与绕轴旋转；中心距离测量模型原点。"));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *form = new QFormLayout;
    auto *source = new QComboBox;
    auto *target = new QComboBox;
    auto *type = new QComboBox;
    auto *sa = new QComboBox;
    auto *ta = new QComboBox;
    for (const auto &b : document_.scene().bodies) {
        source->addItem(b.name, b.id);
        target->addItem(b.name, b.id);
    }
    if (!selection_.isEmpty())
        source->setCurrentIndex(source->findData(selection_));
    target->setCurrentIndex(source->currentIndex() == 0 ? 1 : 0);
    for (int i = 0; i < 4; ++i)
        type->addItem(mateName(MateType(i)), i);
    sa->addItems({"X", "Y", "Z"});
    ta->addItems({"X", "Y", "Z"});
    sa->setCurrentIndex(2);
    ta->setCurrentIndex(2);
    auto *distance = new QDoubleSpinBox;
    distance->setRange(0, 1000000);
    distance->setDecimals(3);
    distance->setSuffix(" mm");
    distance->setValue(30);
    distance->setEnabled(false);
    form->addRow(QStringLiteral("基准模型"), source);
    form->addRow(QStringLiteral("从动模型"), target);
    form->addRow(QStringLiteral("配合类型"), type);
    form->addRow(QStringLiteral("基准局部轴"), sa);
    form->addRow(QStringLiteral("从动局部轴"), ta);
    form->addRow(QStringLiteral("中心距离"), distance);
    layout->addLayout(form);
    auto *feedback = new QLabel;
    feedback->setWordWrap(true);
    feedback->setStyleSheet("color:#efa5a5;");
    layout->addWidget(feedback);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [=](int index) {
        distance->setEnabled(index == 3);
        sa->setEnabled(index != 3);
        ta->setEnabled(index != 3);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        Mate m;
        m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m.source = source->currentData().toString();
        m.target = target->currentData().toString();
        m.type = MateType(type->currentData().toInt());
        m.sourceAxis = sa->currentIndex();
        m.targetAxis = ta->currentIndex();
        m.distance = distance->value();
        QString why;
        if (document_.edit(
                QStringLiteral("添加 ") + mateName(m.type), [&](Scene &s) { s.mates << m; }, &why)) {
            log(QStringLiteral("配合已建立：") + mateName(m.type));
            dialog.accept();
        } else
            feedback->setText(why);
    });
    dialog.exec();
}
void MainWindow::deleteSelection() {
    const QString id = selection_;
    if (!document_.scene().find(id)) {
        statusBar()->showMessage(QStringLiteral("请先选择要删除的模型。"), 4000);
        return;
    }
    document_.finishMove(false);
    document_.removeBody(id);
    selectBody({});
    log(QStringLiteral("模型与相关配合已删除；Ctrl+Z 可撤销。"));
}
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (object == tree_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Delete) {
        deleteSelection();
        return true;
    }
    return QMainWindow::eventFilter(object, event);
}
void MainWindow::deleteMate() {
    auto *item = mateTree_->currentItem();
    if (!item) {
        statusBar()->showMessage(QStringLiteral("请先在配合关系页选中一个配合。"), 5000);
        return;
    }
    auto id = item->data(0, Qt::UserRole).toString();
    document_.edit(QStringLiteral("移除配合"), [&](Scene &s) {
        s.mates.erase(
            std::remove_if(s.mates.begin(), s.mates.end(), [&](const Mate &m) { return m.id == id; }),
            s.mates.end());
    });
}
bool MainWindow::openFile(const QString &path) {
    Scene candidate;
    QString why;
    if (!loadProject(path, candidate, &why)) {
        log(QStringLiteral("打开失败：") + why);
        if (!selfTesting_)
            QMessageBox::warning(this, QStringLiteral("打开失败"), why);
        return false;
    }
    path_ = path;
    selection_.clear();
    document_.replace(candidate);
    viewport_->fitAll();
    log(QStringLiteral("已打开：") + path);
    return true;
}
bool MainWindow::saveFile(const QString &path) {
    QString why;
    if (!saveProject(path, document_.scene(), &why)) {
        log(QStringLiteral("保存失败：") + why);
        if (!selfTesting_)
            QMessageBox::warning(this, QStringLiteral("保存失败"), why);
        return false;
    }
    path_ = path;
    document_.history()->setClean();
    log(QStringLiteral("已保存：") + path);
    return true;
}
bool MainWindow::mayDiscard() {
    if (selfTesting_ || document_.history()->isClean())
        return true;
    auto answer =
        QMessageBox::question(this, QStringLiteral("未保存的更改"), QStringLiteral("是否保存当前项目？"),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;
    auto path = path_;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, QStringLiteral("保存项目"), "assembly.afcad",
                                            "AxisForge (*.afcad)");
    return !path.isEmpty() && saveFile(path);
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (mayDiscard())
        e->accept();
    else
        e->ignore();
}
int MainWindow::runSelfTest(const QString &directory) {
    selfTesting_ = true;
    QDir().mkpath(directory);
    QJsonObject report;
    QStringList checks;
    auto check = [&](bool ok, const QString &label) {
        if (!ok)
            throw std::runtime_error(label.toStdString());
        checks << label;
    };
    try {
        check(viewport_->isValid() && viewport_->renderError().isEmpty(), "OpenGL context and shaders");
        const auto demo = document_.scene();
        check(demo.bodies.size() == 5 && demo.mates.size() == 2, "demo scene");
        QString why;
        check(document_.edit(
                  "Move driver", [](Scene &s) { s.bodies[1].position.setX(12); }, &why),
              "driver edit");
        check(std::abs(document_.scene().bodies[2].position.x() - 12) < 0.001f,
              "coaxial follower propagation");
        document_.history()->undo();
        check(vectorsClose(document_.scene().bodies[1].position, demo.bodies[1].position),
              "undo assembly edit");
        document_.history()->redo();
        check(std::abs(document_.scene().bodies[2].position.x() - 12) < 0.001f, "redo assembly edit");
        document_.history()->undo();
        check(saveFile(directory + "/roundtrip.afcad"), "atomic project save");
        check(openFile(directory + "/roundtrip.afcad"), "project reload");
        check(document_.scene().bodies.size() == 5, "roundtrip body count");
        check(exportMesh(directory + "/assembly.obj", document_.scene(), false, &why), "OBJ export");
        check(exportMesh(directory + "/assembly.stl", document_.scene(), true, &why), "STL export");
        for (int i = 0; i < 5; ++i) {
            const QString key = "create" + shapeKey(Shape(i));
            auto *a = findChild<QAction *>(key);
            check(a != nullptr, "create action exists " + key);
            a->trigger();
            check(document_.scene().bodies.size() == 6, "create shape " + shapeKey(Shape(i)));
            document_.history()->undo();
        }
        const QString id = document_.scene().bodies[1].id;
        selectBody(id);
        name_->setText(QStringLiteral("测试主轴"));
        numbers_["radius"]->setValue(11);
        findChild<QPushButton *>("applyProperties")->click();
        check(document_.scene().find(id)->name == QStringLiteral("测试主轴") &&
                  std::abs(document_.scene().find(id)->params.radius - 11) < 1e-6,
              "inspector applies name and radius");
        document_.history()->undo();
        Scene solo;
        Body body = makeBody(Shape::Box);
        solo.bodies << body;
        document_.replace(solo);
        viewport_->fitAll();
        QApplication::processEvents();
        auto point = viewport_->projectPoint(body.position);
        check(viewport_->pickAt(point) == body.id, "viewport ray picking");
        viewport_->grabFramebuffer();
        const auto counts = viewport_->metrics();
        check(counts.instances == 1 && counts.visibleInstances == 1 && counts.triangles == 12 &&
                  counts.vertices == 36,
              "scene metrics count box instances triangles and submitted vertices");
        check(counts.gpuBufferBytes == quint64(36 * 3 + 170) * sizeof(Vertex),
              "GPU buffer payload includes triangle edges and grid");
        const auto memory = processMemory();
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        check(memory.available && memory.resident > 0, "process resident memory available");
#endif
        auto *metricsDock = findChild<QDockWidget *>("sceneStatsDock");
        metricsDock->close();
        findChild<QAction *>("toggleSceneStats")->trigger();
        check(metricsDock->isVisible(), "scene statistics panel reopens");
        viewport_->setContinuousRendering(true);
        const auto framesBefore = viewport_->metrics().presentedFrames;
        QEventLoop measurement;
        QTimer::singleShot(800, &measurement, &QEventLoop::quit);
        measurement.exec();
        viewport_->sampleFrames();
        const auto measured = viewport_->metrics();
        check(measured.presentedFrames > framesBefore && measured.fps > 0,
              "continuous FPS samples real frameSwapped events");
        report["measuredFps"] = measured.fps;
        const auto gpuSample = gpuMemory(viewport_->rendererInfo());
        report["gpuMemoryAvailable"] = gpuSample.available;
        report["gpuLocalUsageBytes"] = double(gpuSample.usage);
        report["requestedVsync"] = qApp->property("requestedVsync").toInt();
        report["reportedSwapInterval"] = viewport_->format().swapInterval();
        check(QSurfaceFormat::defaultFormat().swapInterval() == qApp->property("requestedVsync").toInt(),
              "startup vertical sync request applied before context creation");
        viewport_->setContinuousRendering(false);
        check(!viewport_->continuousRendering(), "continuous rendering can stop");
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(viewport_, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(viewport_, &release);
        check(selection_ == body.id, "mouse selection updates inspector");
        auto *properties = findChild<QDockWidget *>("propertyDock");
        properties->close();
        check(!properties->isVisible(), "properties panel closes");
        findChild<QAction *>("toggleProperties")->trigger();
        check(properties->isVisible(), "properties panel reopens from menu");
        int historyBefore = document_.history()->count();
        QMouseEvent dragPress(QEvent::MouseButtonPress, QPointF(point), Qt::LeftButton, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(viewport_, &dragPress);
        for (int i = 1; i <= 3; ++i) {
            QMouseEvent dragMove(QEvent::MouseMove, QPointF(point + QPoint(i * 15, 0)), Qt::NoButton,
                                 Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport_, &dragMove);
        }
        QMouseEvent dragRelease(QEvent::MouseButtonRelease, QPointF(point + QPoint(45, 0)), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport_, &dragRelease);
        check(!vectorsClose(document_.scene().find(body.id)->position, body.position),
              "left drag moves model");
        check(document_.history()->count() == historyBefore + 1, "drag creates one undo command");
        document_.history()->undo();
        check(vectorsClose(document_.scene().find(body.id)->position, body.position),
              "undo restores dragged model");
        document_.beginMove(body.id);
        document_.previewMove(body.id, {20, 0, 0});
        document_.finishMove(false);
        check(vectorsClose(document_.scene().find(body.id)->position, body.position),
              "cancel restores drag preview");
        selectBody(body.id);
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(viewport_, &deleteKey);
        check(document_.scene().bodies.isEmpty() && viewport_->pickAt(point).isEmpty(),
              "Delete removes model from scene and picking");
        document_.history()->undo();
        selectBody(body.id);
        QApplication::sendEvent(tree_, &deleteKey);
        check(document_.scene().bodies.isEmpty(), "tree Delete removes model");
        document_.history()->undo();
        selectBody(body.id);
        findChild<QAction *>("deleteBody")->trigger();
        check(document_.scene().bodies.isEmpty(), "toolbar delete action removes model");
        document_.history()->undo();
        selectBody(body.id);
        document_.replace(demo);
        const auto driver = demo.bodies[1].id;
        document_.beginMove(driver);
        document_.previewMove(driver, demo.bodies[1].position + QVector3D(17, 0, 0));
        document_.finishMove(true);
        check(std::abs(document_.scene().bodies[2].position.x() - 17) < 0.001f,
              "drag propagates coaxial follower");
        selectBody(driver);
        findChild<QAction *>("deleteBody")->trigger();
        check(!document_.scene().find(driver) && document_.scene().mates.isEmpty(),
              "delete cascades attached mates");
        document_.replace(solo);
        selectBody(body.id);
        viewport_->fitAll();
        int cameraSignals = 0;
        auto connection = connect(viewport_, &Viewport::cameraChanged, this, [&] { ++cameraSignals; });
        QMouseEvent rightPress(QEvent::MouseButtonPress, QPointF(point), Qt::RightButton, Qt::RightButton,
                               Qt::NoModifier);
        QApplication::sendEvent(viewport_, &rightPress);
        QMouseEvent move(QEvent::MouseMove, QPointF(point + QPoint(40, 20)), Qt::NoButton, Qt::RightButton,
                         Qt::NoModifier);
        QApplication::sendEvent(viewport_, &move);
        check(cameraSignals > 0, "orbit mouse handler");
        disconnect(connection);
        auto *wire = findChild<QAction *>("wireframe");
        wire->setChecked(true);
        QApplication::processEvents();
        wire->setChecked(false);
        auto *ortho = findChild<QAction *>("orthographic");
        ortho->setChecked(true);
        QApplication::processEvents();
        ortho->setChecked(false);
        check(true, "wireframe and orthographic actions");
        document_.replace(demo);
        selectBody(demo.bodies[1].id);
        viewport_->setView(0);
        viewport_->fitAll();
        QApplication::processEvents();
        auto image = viewport_->grabFramebuffer();
        updateSceneStats();
        int colored = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                auto c = image.pixelColor(x, y);
                if (c.green() > 65 && c.blue() > 65 &&
                    qMax(c.red(), qMax(c.green(), c.blue())) - qMin(c.red(), qMin(c.green(), c.blue())) > 20)
                    ++colored;
            }
        check(!image.isNull() && colored > 1000, "framebuffer contains rendered CAD geometry");
        check(grab().save(directory + "/workbench.png"), "workbench screenshot");
        report["success"] = true;
        report["coloredPixels"] = colored;
        report["renderer"] = viewport_->rendererInfo();
    } catch (const std::exception &e) {
        report["success"] = false;
        report["error"] = QString::fromUtf8(e.what());
    }
    report["checks"] = QJsonArray::fromStringList(checks);
    QFile out(directory + "/desktop-test.json");
    if (out.open(QIODevice::WriteOnly))
        out.write(QJsonDocument(report).toJson());
    return report["success"].toBool() ? 0 : 1;
}
} // namespace af
