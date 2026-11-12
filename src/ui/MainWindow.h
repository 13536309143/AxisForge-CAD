#pragma once
#include "app/Document.h"
#include "render/Viewport.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
namespace af {
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    Document *document() {
        return &document_;
    }
    Viewport *viewport() {
        return viewport_;
    }
    void selectBody(const QString &id);
    bool openFile(const QString &path);
    bool saveFile(const QString &path);
    int runSelfTest(const QString &directory);

  protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    void buildActions();
    void buildPanels();
    void refresh();
    void showProperties();
    void applyProperties();
    void addMateDialog();
    void deleteMate();
    void deleteSelection();
    void buildSceneStats();
    void updateSceneStats();
    QLabel *sceneStats_ = nullptr;
    bool mayDiscard();
    void log(const QString &text);
    QString selection_, path_;
    Document document_;
    Viewport *viewport_ = nullptr;
    QTreeWidget *tree_ = nullptr, *mateTree_ = nullptr;
    QWidget *propertyForm_ = nullptr;
    QLineEdit *name_ = nullptr;
    QMap<QString, QDoubleSpinBox *> numbers_;
    QSpinBox *segments_ = nullptr;
    QCheckBox *visible_ = nullptr, *fixed_ = nullptr;
    QLabel *kind_ = nullptr, *stats_ = nullptr, *summary_ = nullptr;
    QPushButton *color_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QColor pendingColor_;
    bool refreshing_ = false, selfTesting_ = false;
};
} // namespace af
