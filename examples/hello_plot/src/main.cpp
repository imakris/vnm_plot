#include "plot_controller.h"

#include <vnm_plot/plot_widget.h>

#include <QtCore/QCoreApplication>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>

#include <cstdlib>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    qmlRegisterType<vnm::plot::Plot_widget>("VnmPlot", 1, 0, "PlotWidget");
    qmlRegisterType<Plot_controller>("Example", 1, 0, "PlotController");

    QQmlApplicationEngine engine;
    const QUrl url("qrc:/qml/main.qml");

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject* obj, const QUrl& obj_url) {
            if (!obj && url == obj_url) {
                QCoreApplication::exit(EXIT_FAILURE);
            }
        },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
