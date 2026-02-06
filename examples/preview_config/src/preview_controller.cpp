#include "preview_controller.h"

#include <glatter/glatter.h>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>

namespace {

constexpr double k_x_min = -20.0;
constexpr double k_x_max = 20.0;
constexpr std::size_t k_main_samples = 4000;
constexpr std::size_t k_preview_samples = 320;
constexpr double k_auto_v_scale = 0.2;
const char* k_vert_shader = ":/vnm_plot/shaders/function_sample.vert";

float sample_signal(double x)
{
    return static_cast<float>(std::sin(x) + 0.35 * std::cos(0.35 * x));
}

} // namespace

Preview_controller::Preview_controller(QObject* parent)
    : QObject(parent)
    , m_main_source(std::make_shared<vnm::plot::Function_data_source>())
    , m_preview_source(std::make_shared<vnm::plot::Function_data_source>())
{
    setup_series();
    generate_samples();
}

void Preview_controller::set_plot_widget(vnm::plot::Plot_widget* widget)
{
    if (m_plot_widget == widget) {
        return;
    }

    if (m_plot_widget) {
        if (m_time_axis_connection) {
            QObject::disconnect(m_time_axis_connection);
            m_time_axis_connection = {};
        }
        QObject::disconnect(m_plot_widget, nullptr, this, nullptr);
        if (m_series) {
            m_plot_widget->remove_series(m_series->id);
        }
    }

    m_plot_widget = widget;

    if (m_plot_widget) {
        configure_plot_widget();
        if (m_series) {
            m_plot_widget->add_series(m_series->id, m_series);
        }
        apply_time_range();
        m_plot_widget->set_v_auto(true);
        m_plot_widget->update();
        m_time_axis_connection = QObject::connect(
            m_plot_widget,
            &vnm::plot::Plot_widget::time_axis_changed,
            this,
            [this]() {
                if (!m_plot_widget) {
                    return;
                }
                apply_time_range();
            });
    }

    emit plot_widget_changed();
}

void Preview_controller::setup_series()
{
    m_series = std::make_shared<vnm::plot::series_data_t>();
    m_series->id = 1;
    m_series->enabled = true;
    m_series->style = vnm::plot::Display_style::LINE;
    m_series->color = glm::vec4(0.25f, 0.85f, 0.55f, 1.0f);

    m_series->access = vnm::plot::make_function_sample_policy();
    m_series->access.layout_key = 0x2001;

    m_series->access.setup_vertex_attributes = []() {
        glVertexAttribLPointer(0, 1, GL_DOUBLE, sizeof(vnm::plot::function_sample_t),
            reinterpret_cast<void*>(offsetof(vnm::plot::function_sample_t, x)));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(vnm::plot::function_sample_t),
            reinterpret_cast<void*>(offsetof(vnm::plot::function_sample_t, y)));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(vnm::plot::function_sample_t),
            reinterpret_cast<void*>(offsetof(vnm::plot::function_sample_t, y_min)));
        glEnableVertexAttribArray(2);

        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(vnm::plot::function_sample_t),
            reinterpret_cast<void*>(offsetof(vnm::plot::function_sample_t, y_max)));
        glEnableVertexAttribArray(3);
    };

    m_series->shader_set = {
        k_vert_shader,
        ":/vnm_plot/shaders/plot_line_adjacency.geom",
        ":/vnm_plot/shaders/plot_line.frag"
    };

    m_series->shaders[vnm::plot::Display_style::AREA] = {
        k_vert_shader,
        ":/vnm_plot/shaders/plot_area.geom",
        ":/vnm_plot/shaders/plot_area.frag"
    };

    m_series->data_source = m_main_source;

    vnm::plot::preview_config_t preview_cfg;
    preview_cfg.data_source = m_preview_source;
    preview_cfg.access = m_series->access;
    preview_cfg.style = vnm::plot::Display_style::AREA;
    m_series->preview_config = preview_cfg;
}

void Preview_controller::configure_plot_widget()
{
    if (!m_plot_widget) {
        return;
    }

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

void Preview_controller::apply_time_range()
{
    if (!m_plot_widget) {
        return;
    }

    m_plot_widget->set_t_range(k_x_min, k_x_max);
    m_plot_widget->set_available_t_range(k_x_min, k_x_max);
}

void Preview_controller::generate_samples()
{
    m_main_source->generate(
        [](double x) {
            return sample_signal(x);
        },
        k_x_min,
        k_x_max,
        k_main_samples);

    m_preview_source->generate(
        [](double x) {
            return sample_signal(x);
        },
        k_x_min,
        k_x_max,
        k_preview_samples);
}
