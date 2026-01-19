#include "plot_controller.h"
#include <glatter/glatter.h>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>

namespace {

constexpr double k_x_min = -10.0;
constexpr double k_x_max = 10.0;
constexpr std::size_t k_num_samples = 2000;
constexpr double k_auto_v_scale = 0.4;
const char* k_vert_shader = ":/vnm_plot/shaders/function_sample.vert";

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
        m_plot_widget->remove_series(m_series->id);
    }

    m_plot_widget = widget;

    if (m_plot_widget) {
        configure_plot_widget();
        if (m_series) {
            m_plot_widget->add_series(m_series->id, m_series);
        }
        m_plot_widget->set_t_range(k_x_min, k_x_max);
        m_plot_widget->set_available_t_range(k_x_min, k_x_max);
        m_plot_widget->set_v_auto(false);
        m_plot_widget->set_v_range(-1.3f, 1.3f);
        m_plot_widget->update();
    }

    emit plot_widget_changed();
}

void Plot_controller::setup_series()
{
    m_series = std::make_shared<vnm::plot::series_data_t>();
    m_series->id = 1;
    m_series->enabled = true;
    m_series->style = vnm::plot::Display_style::LINE;
    m_series->color = glm::vec4(0.2f, 0.7f, 0.9f, 1.0f);

    // Set up access policy for sample extraction
    m_series->access.get_timestamp = [](const void* sample) -> double {
        return static_cast<const vnm::plot::function_sample_t*>(sample)->x;
    };

    m_series->access.get_value = [](const void* sample) -> float {
        return static_cast<const vnm::plot::function_sample_t*>(sample)->y;
    };

    m_series->access.get_range = [](const void* sample) -> std::pair<float, float> {
        const auto* s = static_cast<const vnm::plot::function_sample_t*>(sample);
        return {s->y_min, s->y_max};
    };

    m_series->shader_set = {
        k_vert_shader,
        ":/vnm_plot/shaders/plot_line_adjacency.geom",
        ":/vnm_plot/shaders/plot_line.frag"
    };

    m_series->access.layout_key = 0x1001;

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

    m_series->data_source = std::shared_ptr<vnm::plot::Data_source>(
        &m_data_source,
        [](vnm::plot::Data_source*) {}
    );
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
