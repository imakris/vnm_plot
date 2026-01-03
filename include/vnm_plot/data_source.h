#pragma once

// VNM Plot Library - Data Source Interface
// This is the core abstraction that allows the plot to work with any data source,
// from simple vectors to complex ring buffers with LOD support.

#include "plot_types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// data_snapshot_t: A contiguous view of sample data
// -----------------------------------------------------------------------------
// Represents a snapshot of data that can be safely read. The data pointer
// remains valid for the lifetime of the snapshot (or until the next snapshot
// is taken, depending on the source implementation).
struct data_snapshot_t
{
    const void* data     = nullptr;  // Pointer to first sample
    size_t      count    = 0;        // Number of samples
    size_t      stride   = 0;        // Bytes between samples (sizeof(SampleType))
    uint64_t    sequence = 0;        // Monotonically increasing; changes when data changes

    // Convenience: check if snapshot is valid
    explicit operator bool() const { return data != nullptr && count > 0; }

    // Access sample at index (type-erased)
    const void* at(size_t index) const
    {
        return static_cast<const char*>(data) + index * stride;
    }
};

// Result of try_snapshot() - includes error state
struct snapshot_result_t
{
    data_snapshot_t snapshot;
    enum class Status { OK, EMPTY, BUSY, FAILED } status = Status::OK;

    explicit operator bool() const { return status == Status::OK && snapshot; }
};

// -----------------------------------------------------------------------------
// Data_source: Abstract interface for data sources
// -----------------------------------------------------------------------------
// Implement this interface to provide data to the plot. Examples:
//   - Vector_data_source: backed by std::vector (for function plotting)
//   - Ring_data_source: backed by Lumis's ring buffers with LOD
//   - Stream_data_source: continuously appending data
class Data_source
{
public:
    virtual ~Data_source() = default;

    // --- Core access ---

    // Get a snapshot of the data at the specified LOD level.
    // Level 0 is the finest (full resolution).
    // Returns snapshot_result_t with status indicating success or failure reason.
    virtual snapshot_result_t try_snapshot(size_t lod_level = 0) = 0;

    // Convenience: get snapshot or empty on failure
    data_snapshot_t snapshot(size_t lod_level = 0)
    {
        auto result = try_snapshot(lod_level);
        return result.status == snapshot_result_t::Status::OK ? result.snapshot : data_snapshot_t{};
    }

    // --- LOD (Level of Detail) support ---

    // Number of LOD levels available (1 = no LOD, just full resolution)
    virtual size_t lod_levels() const { return 1; }

    // Scale factor for a given LOD level (how many finest samples per LOD sample)
    // Level 0 always returns 1.
    virtual size_t lod_scale(size_t level) const { (void)level; return 1; }

    // --- Metadata ---

    // Sample stride in bytes (useful for setting up vertex attributes)
    virtual size_t sample_stride() const = 0;

    // Unique identity for this data source (for caching, change detection)
    // Different sources should return different values; same source should be stable.
    virtual const void* identity() const { return this; }
};

// -----------------------------------------------------------------------------
// Data_access_policy: How to extract values from samples
// -----------------------------------------------------------------------------
// This defines how to extract timestamp, value, and range from type-erased samples.
// It also includes GPU rendering configuration (vertex attributes, shaders).
struct Data_access_policy
{
    // --- Sample value extraction (required) ---
    std::function<double(const void* sample)>                get_timestamp;
    std::function<float(const void* sample)>                 get_value;
    std::function<std::pair<float, float>(const void* sample)> get_range;

    // Sample stride (bytes per sample) - must match Data_source::sample_stride()
    size_t sample_stride = 0;

    // --- Optional: auxiliary metric (for colormap rendering) ---
    std::function<double(const void* sample)> get_aux_metric;

    // --- GPU rendering configuration ---

    // Set up OpenGL vertex attributes for this sample type.
    // Called after binding the VBO, before drawing.
    std::function<void()> setup_vertex_attributes;

    // Unique key identifying this vertex layout (for VAO caching)
    uint64_t layout_key = 0;

    // Type aliases to top-level types for backward compatibility.
    // Code using Data_access_policy::Display_style or Data_access_policy::shader_set_t
    // will continue to work, but now refers to the canonical definitions in plot_types.h.
    using shader_set_t = vnm::plot::shader_set_t;
    using Display_style = vnm::plot::Display_style;

    std::map<Display_style, shader_set_t> shaders;

    // Bind additional uniforms before drawing
    std::function<void(unsigned int program_id)> bind_uniforms;

    // Validation: check if policy is properly configured
    bool is_valid() const
    {
        return get_timestamp && get_value && get_range && sample_stride > 0;
    }
};

// -----------------------------------------------------------------------------
// Simple implementations for common use cases
// -----------------------------------------------------------------------------

// Vector_data_source: backed by a vector of samples (simplest case)
// T must be a POD struct with at least a 'timestamp' field for binary search.
template<typename T>
class Vector_data_source : public Data_source
{
public:
    Vector_data_source() = default;

    explicit Vector_data_source(std::vector<T> data)

    :
        m_data(std::move(data))
    {}

    snapshot_result_t try_snapshot(size_t /*lod_level*/ = 0) override
    {
        return {
            data_snapshot_t{m_data.data(), m_data.size(), sizeof(T), m_sequence},
            m_data.empty() ? snapshot_result_t::Status::EMPTY : snapshot_result_t::Status::OK
        };
    }

    size_t sample_stride() const override { return sizeof(T); }

    // Update the data (increments sequence to signal change)
    void set_data(std::vector<T> data)
    {
        m_data = std::move(data);
        ++m_sequence;
    }

    // Direct access for modification
    std::vector<T>&       data()       { return m_data; }
    const std::vector<T>& data() const { return m_data; }

    // Signal that data has changed (call after modifying data())
    void notify_changed() { ++m_sequence; }

    uint64_t sequence() const { return m_sequence; }

private:
    std::vector<T> m_data;
    uint64_t       m_sequence = 0;
};

} // namespace vnm::plot
