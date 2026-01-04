#pragma once

// VNM Plot Library - Core Data Types
// Qt-free data access abstractions for rendering.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec4.hpp>

namespace vnm::plot::core {

// -----------------------------------------------------------------------------
// data_snapshot_t: A contiguous view of sample data
// -----------------------------------------------------------------------------
// Represents a snapshot of data that can be safely read.
struct data_snapshot_t
{
    const void* data     = nullptr;  // Pointer to first sample
    size_t      count    = 0;        // Number of samples
    size_t      stride   = 0;        // Bytes between samples
    uint64_t    sequence = 0;        // Monotonically increasing; changes when data changes

    explicit operator bool() const { return data != nullptr && count > 0; }

    const void* at(size_t index) const
    {
        return static_cast<const char*>(data) + index * stride;
    }
};

// Result of try_snapshot()
struct snapshot_result_t
{
    data_snapshot_t snapshot;
    enum class Status { OK, EMPTY, BUSY, FAILED } status = Status::OK;

    explicit operator bool() const { return status == Status::OK && snapshot; }
};

// -----------------------------------------------------------------------------
// Data_source: Abstract interface for data sources
// -----------------------------------------------------------------------------
class Data_source
{
public:
    virtual ~Data_source() = default;

    virtual snapshot_result_t try_snapshot(size_t lod_level = 0) = 0;

    data_snapshot_t snapshot(size_t lod_level = 0)
    {
        auto result = try_snapshot(lod_level);
        return result.status == snapshot_result_t::Status::OK ? result.snapshot : data_snapshot_t{};
    }

    virtual size_t lod_levels() const { return 1; }
    virtual size_t lod_scale(size_t level) const { (void)level; return 1; }
    virtual size_t sample_stride() const = 0;
    virtual const void* identity() const { return this; }
};

// Forward declaration
struct shader_set_t;

// -----------------------------------------------------------------------------
// Data_access_policy: How to extract values from samples
// -----------------------------------------------------------------------------
struct Data_access_policy
{
    // Sample value extraction
    std::function<double(const void* sample)>                   get_timestamp;
    std::function<float(const void* sample)>                    get_value;
    std::function<std::pair<float, float>(const void* sample)>  get_range;

    size_t sample_stride = 0;

    // Optional: auxiliary metric (for colormap rendering)
    std::function<double(const void* sample)> get_aux_metric;

    // GPU rendering configuration
    std::function<void()> setup_vertex_attributes;
    std::function<void(unsigned int program_id)> bind_uniforms;
    uint64_t layout_key = 0;

    bool is_valid() const
    {
        return get_timestamp && get_value && get_range && sample_stride > 0;
    }
};

// -----------------------------------------------------------------------------
// Display Styles (bit flags for combination)
// -----------------------------------------------------------------------------
enum class Display_style : int
{
    DOTS           = 0x1,
    LINE           = 0x2,
    AREA           = 0x4,
    DOTS_LINE      = DOTS | LINE,
    DOTS_AREA      = DOTS | AREA,
    LINE_AREA      = LINE | AREA,
    DOTS_LINE_AREA = DOTS | LINE | AREA,
    COLORMAP_AREA  = 0x8
};

// Bit operators for Display_style
inline Display_style operator|(Display_style a, Display_style b)
{
    return static_cast<Display_style>(static_cast<int>(a) | static_cast<int>(b));
}

inline Display_style operator&(Display_style a, Display_style b)
{
    return static_cast<Display_style>(static_cast<int>(a) & static_cast<int>(b));
}

inline bool operator!(Display_style s)
{
    return static_cast<int>(s) == 0;
}

// -----------------------------------------------------------------------------
// Shader Set: identifies a shader program by asset names
// -----------------------------------------------------------------------------
struct shader_set_t
{
    std::string vert;  // Vertex shader asset name
    std::string geom;  // Geometry shader asset name (optional)
    std::string frag;  // Fragment shader asset name

    bool operator<(const shader_set_t& other) const
    {
        if (vert != other.vert) return vert < other.vert;
        if (geom != other.geom) return geom < other.geom;
        return frag < other.frag;
    }

    bool operator==(const shader_set_t& other) const
    {
        return vert == other.vert && geom == other.geom && frag == other.frag;
    }

    bool empty() const { return vert.empty() && frag.empty(); }
};

// -----------------------------------------------------------------------------
// Colormap Configuration
// -----------------------------------------------------------------------------
struct colormap_config_t
{
    std::vector<glm::vec4> samples;  // Color samples for the colormap
    uint64_t revision = 0;           // Increment when samples change
};

// -----------------------------------------------------------------------------
// series_data_t: Unified series descriptor using Data_access_policy
// -----------------------------------------------------------------------------
struct series_data_t
{
    int id = 0;
    bool enabled = true;
    Display_style style = Display_style::LINE;
    glm::vec4 color = glm::vec4(0.16f, 0.45f, 0.64f, 1.0f);

    std::shared_ptr<Data_source> data_source;
    Data_access_policy access;  // Unified access policy

    colormap_config_t colormap;

    // Shader configuration
    shader_set_t shader_set;                        // Default shader
    std::map<Display_style, shader_set_t> shaders;  // Per-style shaders (optional)

    // Get shader for a specific style (falls back to default)
    const shader_set_t& shader_for(Display_style s) const
    {
        auto it = shaders.find(s);
        return (it != shaders.end() && !it->second.empty()) ? it->second : shader_set;
    }

    // Convenience accessors that delegate to access policy
    double get_timestamp(const void* sample) const
    {
        return access.get_timestamp ? access.get_timestamp(sample) : 0.0;
    }

    float get_value(const void* sample) const
    {
        return access.get_value ? access.get_value(sample) : 0.0f;
    }

    std::pair<float, float> get_range(const void* sample) const
    {
        return access.get_range ? access.get_range(sample) : std::make_pair(0.0f, 0.0f);
    }

    double get_aux_metric(const void* sample) const
    {
        return access.get_aux_metric ? access.get_aux_metric(sample) : 0.0;
    }
};

} // namespace vnm::plot::core
