// vnm_plot Benchmark - Data Source
// Bridges Ring_buffer to vnm::plot::Data_source interface with zero-copy snapshots

#ifndef VNM_PLOT_BENCHMARK_DATA_SOURCE_H
#define VNM_PLOT_BENCHMARK_DATA_SOURCE_H

#include "ring_buffer.h"
#include "sample_types.h"

#include <vnm_plot/core/types.h>

#include <glatter/glatter.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace vnm::benchmark {

/// Unique layout keys for benchmark sample types
constexpr uint64_t k_bar_sample_layout_key = 0x424152;      // "BAR" in ASCII
constexpr uint64_t k_trade_sample_layout_key = 0x545244;    // "TRD" in ASCII

/// Data source that wraps a Ring_buffer and implements vnm::plot::Data_source.
/// Provides a zero-copy snapshot view into the ring buffer while holding a
/// shared lock, so consumers can read safely without an intermediate copy.
template<typename T>
class Benchmark_data_source : public vnm::plot::Data_source {
public:
    /// Construct with reference to a ring buffer.
    /// @param buffer Ring buffer to read from (must outlive this data source)
    explicit Benchmark_data_source(Ring_buffer<T>& buffer)
        : buffer_(buffer)
    {}

    ~Benchmark_data_source() override = default;

    /// Take a snapshot view of the ring buffer.
    /// Thread-safe: holds a shared lock for the snapshot lifetime.
    vnm::plot::snapshot_result_t try_snapshot(size_t lod_level = 0) override {
        using Status = vnm::plot::snapshot_result_t::Status;

        // Only LOD level 0 is supported
        if (lod_level > 0) {
            return {vnm::plot::data_snapshot_t{}, Status::FAILED};
        }

        auto view = buffer_.view();
        snapshot_sequence_ = view.sequence;

        if (view.count == 0) {
            range_valid_ = false;
            return {vnm::plot::data_snapshot_t{}, Status::EMPTY};
        }

        vnm::plot::data_snapshot_t snapshot;
        snapshot.data = view.data;
        snapshot.count = view.count;
        snapshot.stride = sizeof(T);
        snapshot.sequence = view.sequence;
        snapshot.data2 = view.data2;
        snapshot.count2 = view.count2;
        snapshot.hold = view.lock;

        if (view.sequence != last_buffer_sequence_) {
            update_value_range(snapshot);
            last_buffer_sequence_ = view.sequence;
        }

        return {
            snapshot,
            Status::READY
        };
    }

    size_t sample_stride() const override {
        return sizeof(T);
    }

    /// O(1) value range query (computed during snapshot)
    /// Returns false if snapshot is empty or no data has been processed
    bool has_value_range() const override {
        return range_valid_;
    }

    std::pair<float, float> value_range() const override {
        return {value_min_, value_max_};
    }

    bool value_range_needs_rescan() const override {
        // Range is updated on each snapshot, so no rescan needed
        // But return true if range is invalid to signal need for data
        return !range_valid_;
    }

    /// Get current sequence for change detection
    uint64_t sequence() const {
        return snapshot_sequence_;
    }

private:
    Ring_buffer<T>& buffer_;
    uint64_t snapshot_sequence_ = 0;
    uint64_t last_buffer_sequence_ = 0;
    float value_min_ = std::numeric_limits<float>::max();
    float value_max_ = std::numeric_limits<float>::lowest();
    bool range_valid_ = false;

    /// Update value range from current snapshot data
    void update_value_range(const vnm::plot::data_snapshot_t& snapshot) {
        if (!snapshot || snapshot.count == 0 || snapshot.stride == 0) {
            value_min_ = 0.0f;
            value_max_ = 0.0f;
            range_valid_ = false;
            return;
        }

        value_min_ = std::numeric_limits<float>::max();
        value_max_ = std::numeric_limits<float>::lowest();

        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const void* sample_ptr = snapshot.at(i);
            if (!sample_ptr) {
                continue;
            }
            const auto& sample = *static_cast<const T*>(sample_ptr);
            auto [lo, hi] = get_sample_range(sample);
            value_min_ = std::min(value_min_, lo);
            value_max_ = std::max(value_max_, hi);
        }

        range_valid_ = true;
    }

    /// Extract value range from a sample (specialized per type)
    static std::pair<float, float> get_sample_range(const T& sample);
};

// Specialization for Bar_sample: range is [low, high]
template<>
inline std::pair<float, float> Benchmark_data_source<Bar_sample>::get_sample_range(
    const Bar_sample& sample) {
    return {sample.low, sample.high};
}

// Specialization for Trade_sample: range is [price, price]
template<>
inline std::pair<float, float> Benchmark_data_source<Trade_sample>::get_sample_range(
    const Trade_sample& sample) {
    return {sample.price, sample.price};
}

/// Create a Data_access_policy for Bar_sample
/// Includes vertex attribute setup for function_sample.vert shader compatibility
inline vnm::plot::Data_access_policy make_bar_access_policy() {
    vnm::plot::Data_access_policy policy;

    policy.get_timestamp = [](const void* sample) -> double {
        return static_cast<const Bar_sample*>(sample)->timestamp;
    };

    policy.get_value = [](const void* sample) -> float {
        return static_cast<const Bar_sample*>(sample)->close;
    };

    policy.get_range = [](const void* sample) -> std::pair<float, float> {
        const auto* bar = static_cast<const Bar_sample*>(sample);
        return {bar->low, bar->high};
    };

    policy.get_aux_metric = [](const void* sample) -> double {
        return static_cast<double>(static_cast<const Bar_sample*>(sample)->volume);
    };

    policy.sample_stride = sizeof(Bar_sample);
    policy.layout_key = k_bar_sample_layout_key;

    // Setup vertex attributes matching function_sample.vert:
    // layout(location = 0) in double in_x;      -> timestamp
    // layout(location = 1) in float  in_y;      -> close
    // layout(location = 2) in float  in_y_min;  -> low
    // layout(location = 3) in float  in_y_max;  -> high
    policy.setup_vertex_attributes = []() {
        // Attribute 0: double timestamp (64-bit, uses glVertexAttribLPointer)
        glVertexAttribLPointer(0, 1, GL_DOUBLE, sizeof(Bar_sample),
            reinterpret_cast<void*>(offsetof(Bar_sample, timestamp)));
        glEnableVertexAttribArray(0);

        // Attribute 1: float close (primary value)
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Bar_sample),
            reinterpret_cast<void*>(offsetof(Bar_sample, close)));
        glEnableVertexAttribArray(1);

        // Attribute 2: float low (range min)
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Bar_sample),
            reinterpret_cast<void*>(offsetof(Bar_sample, low)));
        glEnableVertexAttribArray(2);

        // Attribute 3: float high (range max)
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Bar_sample),
            reinterpret_cast<void*>(offsetof(Bar_sample, high)));
        glEnableVertexAttribArray(3);
    };

    return policy;
}

/// Create a Data_access_policy for Trade_sample
/// Includes vertex attribute setup for function_sample.vert shader compatibility
inline vnm::plot::Data_access_policy make_trade_access_policy() {
    vnm::plot::Data_access_policy policy;

    policy.get_timestamp = [](const void* sample) -> double {
        return static_cast<const Trade_sample*>(sample)->timestamp;
    };

    policy.get_value = [](const void* sample) -> float {
        return static_cast<const Trade_sample*>(sample)->price;
    };

    policy.get_range = [](const void* sample) -> std::pair<float, float> {
        const auto* trade = static_cast<const Trade_sample*>(sample);
        return {trade->price, trade->price};
    };

    policy.get_aux_metric = [](const void* sample) -> double {
        return static_cast<double>(static_cast<const Trade_sample*>(sample)->size);
    };

    policy.sample_stride = sizeof(Trade_sample);
    policy.layout_key = k_trade_sample_layout_key;

    // Setup vertex attributes matching function_sample.vert:
    // layout(location = 0) in double in_x;      -> timestamp
    // layout(location = 1) in float  in_y;      -> price
    // layout(location = 2) in float  in_y_min;  -> price (same as y for point data)
    // layout(location = 3) in float  in_y_max;  -> price (same as y for point data)
    policy.setup_vertex_attributes = []() {
        // Attribute 0: double timestamp (64-bit, uses glVertexAttribLPointer)
        glVertexAttribLPointer(0, 1, GL_DOUBLE, sizeof(Trade_sample),
            reinterpret_cast<void*>(offsetof(Trade_sample, timestamp)));
        glEnableVertexAttribArray(0);

        // Attribute 1: float price (primary value)
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Trade_sample),
            reinterpret_cast<void*>(offsetof(Trade_sample, price)));
        glEnableVertexAttribArray(1);

        // Attribute 2: float price (range min = price for point data)
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Trade_sample),
            reinterpret_cast<void*>(offsetof(Trade_sample, price)));
        glEnableVertexAttribArray(2);

        // Attribute 3: float price (range max = price for point data)
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Trade_sample),
            reinterpret_cast<void*>(offsetof(Trade_sample, price)));
        glEnableVertexAttribArray(3);
    };

    return policy;
}

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_DATA_SOURCE_H
