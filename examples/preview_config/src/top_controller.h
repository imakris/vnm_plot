#pragma once

#include <vnm_plot/vnm_plot.h>

#include <QtCore/QObject>

#include <memory>

class Top_controller : public QObject
{
    Q_OBJECT

    Q_PROPERTY(vnm::plot::Plot_widget* plot_widget READ plot_widget WRITE set_plot_widget NOTIFY plot_widget_changed)

public:
    explicit Top_controller(QObject* parent = nullptr);

    vnm::plot::Plot_widget* plot_widget() const { return m_plot_widget; }
    void set_plot_widget(vnm::plot::Plot_widget* widget);

signals:
    void plot_widget_changed();

private:
    void setup_series();
    void configure_plot_widget();
    void apply_time_range();
    void generate_samples();

    vnm::plot::Plot_widget* m_plot_widget = nullptr;
    std::shared_ptr<vnm::plot::Function_data_source> m_data_source;
    std::shared_ptr<vnm::plot::series_data_t> m_series;
    QMetaObject::Connection m_time_axis_connection;
};
