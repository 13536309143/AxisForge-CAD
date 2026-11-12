#include "ui/MainWindow.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    QApplication::setApplicationName("AxisForge CAD");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("AxisForge");
    af::MainWindow window;
    window.show();
    const auto args = app.arguments();
    int testIndex = args.indexOf("--self-test");
    if (testIndex >= 0) {
        QString directory = testIndex + 1 < args.size() ? args[testIndex + 1] : "test-artifacts";
        QTimer::singleShot(1200, &window,
                           [&app, &window, directory] { app.exit(window.runSelfTest(directory)); });
    } else if (args.size() > 1)
        window.openFile(args[1]);
    return app.exec();
}
