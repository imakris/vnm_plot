#pragma once

// VNM Plot Library - Series Data
// Data descriptor used by Plot_widget to render series.

#include "../data_source.h"
#include "../plot_types.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Series Data
// -----------------------------------------------------------------------------
// Represents a single data series to be rendered.
struct series_data_t
{
    int id = 0;
    bool enabled = true;
    Display_style style = Display_style::LINE;
    glm::vec4 color = glm::vec4(0.16f, 0.45f, 0.64f, 1.0f);

    std::shared_ptr<Data_source> data_source;
    colormap_config_t colormap;

    // Sample access (required for binary search and rendering)
    std::function<double(const void* sample)> get_timestamp;
    std::function<float(const void* sample)> get_value;
    std::function<std::pair<float, float>(const void* sample)> get_range;
    std::function<double(const void* sample)> get_aux_metric;

    // Shader configuration
    shader_set_t shader_set;
    // Optional per-style shaders; fallback to shader_set when missing.
    std::map<Display_style, shader_set_t> shader_sets;
    std::function<void()> setup_vertex_attributes;
    std::function<void(unsigned int program_id)> bind_uniforms;
    std::uint64_t layout_key = 0;
};

} // namespace vnm::plot
