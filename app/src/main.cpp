#include <QApplication>
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    app.setApplicationName("ScopeAgent");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("ScopeAgent");

    MainWindow w;
    w.show();
    return app.exec();
}
