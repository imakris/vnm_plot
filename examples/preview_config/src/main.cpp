#include "preview_controller.h"
#include "top_controller.h"

#include <vnm_plot/vnm_plot.h>

#include <QtCore/QCoreApplication>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuick/QQuickWindow>
#include <QSurfaceFormat>

#include <cstdlib>

int main(int argc, char* argv[])
{
    QSurfaceFormat surface_format = QSurfaceFormat::defaultFormat();
    surface_format.setSamples(8);
    QSurfaceFormat::setDefaultFormat(surface_format);

    QGuiApplication app(argc, argv);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    qmlRegisterType<vnm::plot::Plot_widget>("VnmPlot", 1, 0, "PlotWidget");
    qmlRegisterType<vnm::plot::Plot_interaction_item>("VnmPlot", 1, 0, "PlotInteractionItem");
    qmlRegisterType<vnm::plot::Plot_time_axis>("VnmPlot", 1, 0, "PlotTimeAxis");
    qmlRegisterType<Preview_controller>("Example", 1, 0, "PreviewController");
    qmlRegisterType<Top_controller>("Example", 1, 0, "TopController");

    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/vnm_plot/qml");
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
