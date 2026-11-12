#include "ui/MainWindow.h"
#include <QApplication>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTimer>
int main(int argc, char **argv) {
    QSettings settings("AxisForge", "AxisForge CAD");
    int vsync = settings.value("render/vsync", 1).toInt() == 0 ? 0 : 1;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--vsync=0")
            vsync = 0;
        if (QString::fromLocal8Bit(argv[i]) == "--vsync=1")
            vsync = 1;
    }
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    format.setSwapInterval(vsync);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    app.setProperty("requestedVsync", vsync);
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
    } else {
        for (int i = 1; i < args.size(); ++i)
            if (!args[i].startsWith("--")) {
                window.openFile(args[i]);
                break;
            }
    }
    return app.exec();
}
