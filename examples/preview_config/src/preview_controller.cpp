#include "preview_controller.h"

#include <QtCore/QtGlobal>

#include <cmath>
#include <cstddef>
#include <utility>

namespace {

namespace plot_examples = vnm::plot::examples;

constexpr double k_x_min = -20.0;
constexpr double k_x_max = 20.0;
// Plot_view::t_range / t_available_range are int64 nanoseconds; sample x
// values from the example Function_data_source come in as fp seconds and the access
// policy auto-converts at the data boundary, but the view-range setters
// take qint64 directly and need explicit conversion here.
constexpr qint64 k_ns_per_second = 1'000'000'000;
constexpr qint64 k_t_min_ns = static_cast<qint64>(k_x_min * static_cast<double>(k_ns_per_second));
constexpr qint64 k_t_max_ns = static_cast<qint64>(k_x_max * static_cast<double>(k_ns_per_second));
constexpr std::size_t k_main_samples = 4000;
constexpr std::size_t k_preview_samples = 320;
constexpr double k_auto_v_scale = 0.2;
constexpr int k_series_id = 1;

float sample_signal(double x)
{
    return static_cast<float>(std::sin(x) + 0.35 * std::cos(0.35 * x));
}

} // namespace

Preview_controller::Preview_controller(QObject* parent)
    : QObject(parent)
    , m_main_source(std::make_shared<plot_examples::Function_data_source>())
    , m_preview_source(std::make_shared<plot_examples::Function_data_source>())
{
    setup_series();
    generate_samples();
}

void Preview_controller::set_plot_widget(vnm::plot::Plot_widget* widget)
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
        view.t_range = std::make_pair(k_t_min_ns, k_t_max_ns);
        view.t_available_range = std::make_pair(k_t_min_ns, k_t_max_ns);
        view.v_auto = true;
        m_plot_widget->set_view(view);
        m_plot_widget->update();
        m_time_axis_connection = QObject::connect(
            m_plot_widget, &vnm::plot::Plot_widget::time_axis_changed, this,
            [this]() {
                if (!m_plot_widget) return;
                vnm::plot::Plot_view view;
                view.t_range = std::make_pair(k_t_min_ns, k_t_max_ns);
                view.t_available_range = std::make_pair(k_t_min_ns, k_t_max_ns);
                m_plot_widget->set_view(view);
            });
    }

    emit plot_widget_changed();
}

void Preview_controller::setup_series()
{
    m_series = vnm::plot::Series_builder()
        .enabled(true)
        .style(vnm::plot::Display_style::LINE)
        .color(vnm::plot::rgba_u8(64, 217, 140))
        .data_source(m_main_source)
        .access(plot_examples::make_function_sample_policy_typed())
        .build_shared();

    vnm::plot::preview_config_t preview_cfg;
    preview_cfg.data_source = m_preview_source;
    preview_cfg.access = m_series->access;
    preview_cfg.style = vnm::plot::Display_style::AREA;
    m_series->preview_config = preview_cfg;
}

void Preview_controller::configure_plot_widget()
{
    if (!m_plot_widget) return;

    vnm::plot::Plot_config config;
    config.dark_mode = true;
    config.clear_to_transparent = true;
    config.show_text = true;
    config.font_size_px = 11.0;
    config.preview_height_px = 120.0;
    config.preview_visibility = 0.6;
    config.line_width_px = 1.35;
    config.auto_v_range_extra_scale = k_auto_v_scale;
    m_plot_widget->set_config(config);
}

void Preview_controller::generate_samples()
{
    auto gen = [](double x) { return sample_signal(x); };
    m_main_source->generate(gen, k_x_min, k_x_max, k_main_samples);
    m_preview_source->generate(gen, k_x_min, k_x_max, k_preview_samples);
}
