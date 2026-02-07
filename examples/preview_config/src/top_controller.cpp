#include "top_controller.h"
#include <cmath>
#include <cstddef>
#include <utility>

namespace {

constexpr double k_x_min = -20.0;
constexpr double k_x_max = 20.0;
constexpr std::size_t k_samples = 1800;
constexpr float k_signal_scale = 250.0f;
constexpr int k_series_id = 2;

float sample_signal(double x)
{
    return k_signal_scale *
        static_cast<float>(2.2 * std::cos(0.4 * x) - 0.6 * std::sin(1.6 * x));
}

} // namespace

Top_controller::Top_controller(QObject* parent)
    : QObject(parent)
    , m_data_source(std::make_shared<vnm::plot::Function_data_source>())
{
    setup_series();
    generate_samples();
}

void Top_controller::set_plot_widget(vnm::plot::Plot_widget* widget)
{
    if (m_plot_widget == widget) return;

    if (m_plot_widget) {
        if (m_time_axis_connection) {
            QObject::disconnect(m_time_axis_connection);
            m_time_axis_connection = {};
        }
        QObject::disconnect(m_plot_widget, nullptr, this, nullptr);
        if (m_series) m_plot_widget->remove_series(k_series_id);
    }

    m_plot_widget = widget;

    if (m_plot_widget) {
        configure_plot_widget();
        if (m_series) m_plot_widget->add_series(k_series_id, m_series);
        vnm::plot::Plot_view view;
        view.t_range = std::make_pair(k_x_min, k_x_max);
        view.t_available_range = std::make_pair(k_x_min, k_x_max);
        view.v_auto = false;
        view.v_range = std::make_pair(-800.0f, 800.0f);
        m_plot_widget->set_view(view);
        m_plot_widget->update();
        m_time_axis_connection = QObject::connect(
            m_plot_widget, &vnm::plot::Plot_widget::time_axis_changed, this,
            [this]() {
                if (!m_plot_widget) return;
                vnm::plot::Plot_view view;
                view.t_range = std::make_pair(k_x_min, k_x_max);
                view.t_available_range = std::make_pair(k_x_min, k_x_max);
                m_plot_widget->set_view(view);
            });
    }

    emit plot_widget_changed();
}

void Top_controller::setup_series()
{
    m_series = vnm::plot::Series_builder()
        .enabled(true)
        .style(vnm::plot::Display_style::AREA)
        .color(vnm::plot::rgba_u8(230, 89, 51, 230))
        .data_source(m_data_source)
        .access(vnm::plot::make_function_sample_policy_typed())
        .build_shared();
}

void Top_controller::configure_plot_widget()
{
    if (!m_plot_widget) return;

    vnm::plot::Plot_config config;
    config.dark_mode = false;
    config.clear_to_transparent = true;
    config.show_text = true;
    config.font_size_px = 10.0;
    config.preview_height_px = 0.0;
    config.preview_visibility = 0.0;
    config.line_width_px = 1.6;
    m_plot_widget->set_config(config);
    m_plot_widget->set_relative_preview_height(0.0f);
    m_plot_widget->set_preview_height_min(0.0);
    m_plot_widget->set_preview_height_max(0.0);
    m_plot_widget->set_show_if_calculated_preview_height_below_min(false);
}

void Top_controller::generate_samples()
{
    m_data_source->generate(
        [](double x) { return sample_signal(x); },
        k_x_min, k_x_max, k_samples);
}
