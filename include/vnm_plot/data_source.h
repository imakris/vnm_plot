#pragma once

// VNM Plot Library - Data Source Interface
// This is the core abstraction that allows the plot to work with any data source,
// from simple vectors to complex ring buffers with LOD support.
//
// This header re-exports core types and provides wrapper-specific utilities.

#include <vnm_plot/core/data_types.h>
#include "plot_types.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Re-exported Core Types
// -----------------------------------------------------------------------------
// These types are defined in core and re-exported here for wrapper-level code.
using data_snapshot_t   = core::data_snapshot_t;
using snapshot_result_t = core::snapshot_result_t;
using Data_source       = core::Data_source;
using Data_access_policy = core::Data_access_policy;
using series_data_t     = core::series_data_t;

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
