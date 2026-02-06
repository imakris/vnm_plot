#include "top_controller.h"

#include <glatter/glatter.h>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>

namespace {

constexpr double k_x_min = -20.0;
constexpr double k_x_max = 20.0;
constexpr std::size_t k_samples = 1800;
constexpr float k_signal_scale = 250.0f;
const char* k_vert_shader = ":/vnm_plot/shaders/function_sample.vert";

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
        m_plot_widget->set_v_auto(false);
        m_plot_widget->set_v_range(-800.0f, 800.0f);
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

void Top_controller::setup_series()
{
    m_series = std::make_shared<vnm::plot::series_data_t>();
    m_series->id = 2;
    m_series->enabled = true;
    m_series->style = vnm::plot::Display_style::AREA;
    m_series->color = glm::vec4(0.9f, 0.35f, 0.2f, 0.9f);

    m_series->access = vnm::plot::make_function_sample_policy();
    m_series->access.layout_key = 0x3001;

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

    m_series->data_source = m_data_source;
}

void Top_controller::configure_plot_widget()
{
    if (!m_plot_widget) {
        return;
    }

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

void Top_controller::apply_time_range()
{
    if (!m_plot_widget) {
        return;
    }

    m_plot_widget->set_t_range(k_x_min, k_x_max);
    m_plot_widget->set_available_t_range(k_x_min, k_x_max);
}

void Top_controller::generate_samples()
{
    m_data_source->generate(
        [](double x) {
            return sample_signal(x);
        },
        k_x_min,
        k_x_max,
        k_samples);
}
