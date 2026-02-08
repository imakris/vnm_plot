#include <vnm_plot/core/default_shaders.h>
#include <vnm_plot/core/function_sample.h>

namespace vnm::plot {
namespace {

const shader_set_t k_empty_shader_set{};

const shader_set_t k_function_dots{
    "shaders/function_sample.vert",
    "shaders/plot_dot_quad.geom",
    "shaders/plot_dot_quad.frag"
};

const shader_set_t k_function_line{
    "shaders/function_sample.vert",
    "shaders/plot_line_adjacency.geom",
    "shaders/plot_line.frag"
};

const shader_set_t k_function_area{
    "shaders/function_sample.vert",
    "shaders/plot_area.geom",
    "shaders/plot_area.frag"
};

const shader_set_t k_function_colormap_line{
    "shaders/function_sample.vert",
    "shaders/plot_colormap_line_adjacency.geom",
    "shaders/plot_colormap_line.frag"
};

} // anonymous namespace

const shader_set_t& default_shader_for_layout(uint64_t layout_key, Display_style style)
{
    if (layout_key != function_sample_layout_key()) {
        return k_empty_shader_set;
    }

    if (style == Display_style::DOTS) {
        return k_function_dots;
    }
    if (style == Display_style::LINE) {
        return k_function_line;
    }
    if (style == Display_style::AREA) {
        return k_function_area;
    }
    if (style == Display_style::COLORMAP_LINE) {
        return k_function_colormap_line;
    }

    return k_empty_shader_set;
}

} // namespace vnm::plot
