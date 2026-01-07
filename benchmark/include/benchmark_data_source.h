// vnm_plot Benchmark - Data Source
// Bridges Ring_buffer to vnm::plot::Data_source interface with copy-on-snapshot

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
/// Uses copy-on-snapshot pattern: data is copied from ring buffer to owned
/// storage when try_snapshot() is called, ensuring thread-safe reads.
template<typename T>
class Benchmark_data_source : public vnm::plot::Data_source {
public:
    /// Construct with reference to a ring buffer.
    /// @param buffer Ring buffer to read from (must outlive this data source)
    explicit Benchmark_data_source(Ring_buffer<T>& buffer)
        : buffer_(buffer)
    {}

    ~Benchmark_data_source() override = default;

    /// Take a snapshot by copying from ring buffer.
    /// Thread-safe: ring buffer holds lock during copy.
    /// Uses incremental copying when possible (O(new_samples) vs O(all_samples)).
    vnm::plot::snapshot_result_t try_snapshot(size_t lod_level = 0) override {
        using Status = vnm::plot::snapshot_result_t::Status;

        // Only LOD level 0 is supported
        if (lod_level > 0) {
            return {vnm::plot::data_snapshot_t{}, Status::FAILED};
        }

        // Short-circuit if sequence hasn't changed (avoid redundant copy)
        // Note: This is safe because if sequence matches, no new samples were added
        uint64_t current_seq = buffer_.sequence();
        if (current_seq == last_buffer_sequence_ && !snapshot_data_.empty()) {
            return {
                vnm::plot::data_snapshot_t{
                    snapshot_data_.data(),
                    snapshot_data_.size(),
                    sizeof(T),
                    snapshot_sequence_
                },
                Status::OK
            };
        }

        const std::size_t buffer_capacity = buffer_.capacity();

        // Try incremental copy using atomic delta computation under lock.
        // Only use incremental mode when:
        // 1. We have existing data at full capacity (steady state, not ramp-up)
        // 2. Snapshot size matches buffer capacity (buffer is full)
        const bool try_incremental =
            !snapshot_data_.empty() &&
            snapshot_data_.size() == buffer_capacity;

        if (try_incremental) {
            // Use atomic copy_newest_since to avoid race between sequence read and copy
            const std::size_t max_new = buffer_capacity / 2;
            auto result = buffer_.copy_newest_since(
                new_samples_buffer_.data(),
                max_new,
                last_buffer_sequence_);

            // Compute actual samples added since last snapshot
            const uint64_t samples_added = result.sequence - last_buffer_sequence_;

            // Only proceed with incremental if:
            // - Buffer is full (at capacity) so sliding window is stable
            // - We got all new samples (samples_added <= max_new)
            // - samples_added is reasonable (< buffer_capacity, guards against wrap)
            const bool incremental_valid =
                result.total_count == buffer_capacity &&
                samples_added > 0 &&
                samples_added <= max_new &&
                samples_added < buffer_capacity &&
                result.copied == static_cast<std::size_t>(samples_added);

            if (incremental_valid) {
                const std::size_t samples_to_add = result.copied;
                const std::size_t keep_count = snapshot_data_.size() - samples_to_add;

                // Check if any evicted samples were at range boundaries
                // If so, we need a full range rescan after the update
                bool need_range_rescan = false;
                if (range_valid_) {
                    for (std::size_t i = 0; i < samples_to_add; ++i) {
                        auto [lo, hi] = get_sample_range(snapshot_data_[i]);
                        if (lo <= value_min_ || hi >= value_max_) {
                            need_range_rescan = true;
                            break;
                        }
                    }
                }

                // Shift remaining samples to the front using memmove
                if (keep_count > 0) {
                    std::memmove(snapshot_data_.data(),
                                 snapshot_data_.data() + samples_to_add,
                                 keep_count * sizeof(T));
                }

                // Copy new samples from the temporary buffer to the end
                std::memcpy(snapshot_data_.data() + keep_count,
                            new_samples_buffer_.data(),
                            samples_to_add * sizeof(T));

                snapshot_sequence_ = result.sequence;
                last_buffer_sequence_ = result.sequence;

                // Update value range
                if (need_range_rescan) {
                    range_valid_ = false;  // Force full rescan
                }
                update_value_range(samples_to_add);

                return {
                    vnm::plot::data_snapshot_t{
                        snapshot_data_.data(),
                        snapshot_data_.size(),
                        sizeof(T),
                        snapshot_sequence_
                    },
                    Status::OK
                };
            }
        }

        // Fall back to full copy (ramp-up, size change, or incremental failed)
        auto result = buffer_.copy_to(snapshot_data_);
        snapshot_sequence_ = result.sequence;
        last_buffer_sequence_ = result.sequence;

        // Ensure new_samples_buffer_ has capacity for future incremental copies
        if (new_samples_buffer_.size() < buffer_capacity / 2) {
            new_samples_buffer_.resize(buffer_capacity / 2);
        }

        if (result.count == 0) {
            range_valid_ = false;
            return {vnm::plot::data_snapshot_t{}, Status::EMPTY};
        }

        // Full copy requires full range scan
        range_valid_ = false;
        update_value_range(0);

        return {
            vnm::plot::data_snapshot_t{
                snapshot_data_.data(),
                snapshot_data_.size(),
                sizeof(T),
                snapshot_sequence_
            },
            Status::OK
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

    /// Get snapshot data for direct access (after try_snapshot)
    const std::vector<T>& snapshot_data() const {
        return snapshot_data_;
    }

private:
    Ring_buffer<T>& buffer_;
    std::vector<T> snapshot_data_;
    std::vector<T> new_samples_buffer_;  // Temp buffer for incremental copies
    uint64_t snapshot_sequence_ = 0;
    uint64_t last_buffer_sequence_ = 0;
    float value_min_ = std::numeric_limits<float>::max();
    float value_max_ = std::numeric_limits<float>::lowest();
    bool range_valid_ = false;

    /// Update value range, scanning new samples or doing full rescan if needed.
    /// @param new_sample_count Number of new samples at the end (0 = full rescan)
    void update_value_range(std::size_t new_sample_count) {
        if (snapshot_data_.empty()) {
            value_min_ = 0.0f;
            value_max_ = 0.0f;
            range_valid_ = false;
            return;
        }

        // If range is invalid or no incremental info, do full scan
        if (!range_valid_ || new_sample_count == 0) {
            value_min_ = std::numeric_limits<float>::max();
            value_max_ = std::numeric_limits<float>::lowest();
            for (const auto& sample : snapshot_data_) {
                auto [lo, hi] = get_sample_range(sample);
                value_min_ = std::min(value_min_, lo);
                value_max_ = std::max(value_max_, hi);
            }
        } else {
            // Incremental scan: only check the newest samples at the end
            const std::size_t start_idx = snapshot_data_.size() - new_sample_count;
            for (std::size_t i = start_idx; i < snapshot_data_.size(); ++i) {
                auto [lo, hi] = get_sample_range(snapshot_data_[i]);
                value_min_ = std::min(value_min_, lo);
                value_max_ = std::max(value_max_, hi);
            }
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
