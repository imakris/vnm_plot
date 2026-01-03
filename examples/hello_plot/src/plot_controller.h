#pragma once

#include <vnm_plot/function_sample.h>
#include <vnm_plot/plot_widget.h>
#include <vnm_plot/renderers/series_renderer.h>

#include <QtCore/QObject>

#include <memory>

class Plot_controller : public QObject
{
    Q_OBJECT

    Q_PROPERTY(vnm::plot::Plot_widget* plot_widget READ plot_widget WRITE set_plot_widget NOTIFY plot_widget_changed)

public:
    explicit Plot_controller(QObject* parent = nullptr);

    vnm::plot::Plot_widget* plot_widget() const { return m_plot_widget; }
    void set_plot_widget(vnm::plot::Plot_widget* widget);

signals:
    void plot_widget_changed();

private:
    void setup_series();
    void configure_plot_widget();
    void generate_samples();

    vnm::plot::Plot_widget* m_plot_widget = nullptr;
    vnm::plot::Function_data_source m_data_source;
    std::shared_ptr<vnm::plot::series_data_t> m_series;
};
