#include "plot_controller.h"
#include <vnm_plot/core/series_builder.h>

#include <cmath>
#include <utility>

namespace {

constexpr double k_x_min = -10.0;
constexpr double k_x_max = 10.0;
constexpr std::size_t k_num_samples = 2000;
constexpr double k_auto_v_scale = 0.4;
constexpr int k_series_id = 1;

} // namespace

Plot_controller::Plot_controller(QObject* parent)

:
    QObject(parent)
{
    setup_series();
    generate_samples();
}

void Plot_controller::set_plot_widget(vnm::plot::Plot_widget* widget)
{
    if (m_plot_widget == widget) {
        return;
    }

    if (m_plot_widget && m_series) {
        m_plot_widget->remove_series(k_series_id);
    }

    m_plot_widget = widget;

    if (m_plot_widget) {
        configure_plot_widget();
        if (m_series) {
            m_plot_widget->add_series(k_series_id, m_series);
        }
        vnm::plot::Plot_view view;
        view.t_range = std::make_pair(k_x_min, k_x_max);
        view.t_available_range = std::make_pair(k_x_min, k_x_max);
        view.v_auto = false;
        view.v_range = std::make_pair(-1.3f, 1.3f);
        m_plot_widget->set_view(view);
        m_plot_widget->update();
    }

    emit plot_widget_changed();
}

void Plot_controller::setup_series()
{
    m_series = vnm::plot::Series_builder()
        .enabled(true)
        .style(vnm::plot::Display_style::LINE)
        .color(vnm::plot::rgba_u8(51, 179, 230))
        .data_source_ref(m_data_source)
        .access(vnm::plot::make_function_sample_policy_typed())
        .build_shared();
}

void Plot_controller::configure_plot_widget()
{
    if (!m_plot_widget) {
        return;
    }

    vnm::plot::Plot_config config;
    config.dark_mode = true;
    config.clear_to_transparent = true;
    config.show_text = true;
    config.font_size_px = 11.0;
    config.preview_height_px = 0;
    config.line_width_px = 1.25;
    config.auto_v_range_extra_scale = k_auto_v_scale;

    m_plot_widget->set_config(config);
}

void Plot_controller::generate_samples()
{
    m_data_source.generate(
        [](double x) {
            return static_cast<float>(std::sin(x));
        },
        k_x_min,
        k_x_max,
        k_num_samples);
}
