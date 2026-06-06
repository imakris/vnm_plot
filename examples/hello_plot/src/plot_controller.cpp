#include "plot_controller.h"
#include <vnm_plot/core/series_builder.h>

#include <QtCore/QtGlobal>

#include <cmath>
#include <utility>

namespace {

namespace plot_examples = vnm::plot::examples;

constexpr double k_x_min = -10.0;
constexpr double k_x_max = 10.0;
// vnm_plot's view-range API works in int64 nanoseconds (the access policy
// for the example function sample auto-converts the fp seconds member at the data
// boundary, but Plot_view::t_range is on the C++ qint64 surface and has
// to be set explicitly).
constexpr qint64 k_ns_per_second = 1'000'000'000;
constexpr qint64 k_t_min_ns = static_cast<qint64>(k_x_min * static_cast<double>(k_ns_per_second));
constexpr qint64 k_t_max_ns = static_cast<qint64>(k_x_max * static_cast<double>(k_ns_per_second));
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
        view.t_range = std::make_pair(k_t_min_ns, k_t_max_ns);
        view.t_available_range = std::make_pair(k_t_min_ns, k_t_max_ns);
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
        .access(plot_examples::make_function_sample_policy_typed())
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
            const double shoulder = 0.34 * std::exp(-0.55 * (x - 3.2) * (x - 3.2));
            const double trough   = 0.27 * std::exp(-0.42 * (x + 4.8) * (x + 4.8));
            return static_cast<float>(0.0012 * x * x * x - 0.04 * x + shoulder - trough);
        },
        k_x_min,
        k_x_max,
        k_num_samples);
}
