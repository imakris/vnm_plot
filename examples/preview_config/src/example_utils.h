#pragma once

#include <vnm_plot/vnm_plot.h>
#include <glatter/glatter.h>

inline void setup_function_sample_vertex_attributes()
{
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
}

constexpr const char* k_vert_shader = ":/vnm_plot/shaders/function_sample.vert";

inline vnm::plot::shader_set_t line_shader_set()
{
    return {k_vert_shader, ":/vnm_plot/shaders/plot_line_adjacency.geom",
            ":/vnm_plot/shaders/plot_line.frag"};
}

inline vnm::plot::shader_set_t area_shader_set()
{
    return {k_vert_shader, ":/vnm_plot/shaders/plot_area.geom",
            ":/vnm_plot/shaders/plot_area.frag"};
}
