// vnm_plot Benchmark - Ring Buffer
// Thread-safe, fixed-capacity circular buffer for streaming data

#ifndef VNM_PLOT_BENCHMARK_RING_BUFFER_H
#define VNM_PLOT_BENCHMARK_RING_BUFFER_H

#include <vnm_plot/core/plot_config.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace vnm::benchmark {

/// Thread-safe circular buffer with overwriting semantics.
/// Producer pushes samples, consumer copies under lock.
/// When full, oldest data is overwritten (sliding window).
template<typename T>
class Ring_buffer {
public:
    struct Statistics {
        std::size_t occupancy = 0;
        std::size_t high_water_occupancy = 0;
        std::uint64_t revision = 0;
        std::uint64_t published_samples = 0;
        std::uint64_t overwritten_samples = 0;
        std::uint64_t producer_wait_count = 0;
        std::uint64_t producer_wait_total_ns = 0;
        std::uint64_t producer_wait_min_ns = 0;
        std::uint64_t producer_wait_max_ns = 0;
    };

    /// Result of a copy operation
    struct Copy_result {
        std::size_t count;    ///< Number of samples copied
        uint64_t sequence;    ///< Sequence at time of copy
    };

    /// Result of a view operation (zero-copy snapshot)
    struct View_result {
        const T* data = nullptr;
        std::size_t count = 0;
        const T* data2 = nullptr;
        std::size_t count2 = 0;
        uint64_t sequence = 0;
        std::shared_ptr<std::shared_lock<std::shared_mutex>> lock;
    };

    /// Construct a ring buffer with the given capacity.
    /// @param capacity Maximum number of elements (must be > 0)
    explicit Ring_buffer(std::size_t capacity)
        : m_buffer(capacity)
    {
        // Capacity of 0 would cause division by zero
        if (capacity == 0) {
            m_buffer.resize(1);
        }
    }

    // Non-copyable, non-movable (mutex not movable)
    Ring_buffer(const Ring_buffer&) = delete;
    Ring_buffer& operator=(const Ring_buffer&) = delete;
    Ring_buffer(Ring_buffer&&) = delete;
    Ring_buffer& operator=(Ring_buffer&&) = delete;

    /// Optional profiler hookup for copy_to() timing.
    void set_profiler(vnm::plot::Profiler* profiler) noexcept {
        m_profiler = profiler;
    }

    // -------------------------------------------------------------------------
    // Producer API (data generator thread)
    // -------------------------------------------------------------------------

    /// Push a single sample. Overwrites oldest if full.
    void push(const T& sample) {
        const auto wait_started = std::chrono::steady_clock::now();
        std::unique_lock lock(m_mutex);
        const double wait_ns = producer_wait_ns(wait_started);

        const std::size_t cap = m_buffer.size();
        const std::size_t h = m_head.load(std::memory_order_relaxed);
        const std::size_t t = m_tail.load(std::memory_order_relaxed);
        const std::size_t count = m_count.load(std::memory_order_relaxed);

        const bool was_full = count == cap;

        m_buffer[h] = sample;
        m_head.store((h + 1) % cap, std::memory_order_relaxed);

        if (was_full) {
            m_tail.store((t + 1) % cap, std::memory_order_relaxed);
            m_overwritten_samples.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            const std::size_t new_count = count + 1;
            m_count.store(new_count, std::memory_order_relaxed);
            update_high_water(new_count);
        }

        m_published_samples.fetch_add(1, std::memory_order_relaxed);
        publish_next_revision();
        record_producer_wait(wait_ns);
        lock.unlock();
    }

    /// Push multiple samples. Overwrites oldest as needed.
    void push_batch(const T* samples, std::size_t count) {
        if (count == 0 || samples == nullptr) {
            return;
        }

        const auto wait_started = std::chrono::steady_clock::now();
        std::unique_lock lock(m_mutex);
        const double wait_ns = producer_wait_ns(wait_started);

        const std::size_t cap = m_buffer.size();
        std::size_t occupancy = m_count.load(std::memory_order_relaxed);
        std::size_t overwritten = 0;
        std::uint64_t revision = m_revision.load(std::memory_order_relaxed);

        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t h = m_head.load(std::memory_order_relaxed);
            const std::size_t t = m_tail.load(std::memory_order_relaxed);
            const bool was_full = occupancy == cap;

            m_buffer[h] = samples[i];
            m_head.store((h + 1) % cap, std::memory_order_relaxed);

            if (was_full) {
                m_tail.store((t + 1) % cap, std::memory_order_relaxed);
                ++overwritten;
            }
            else {
                ++occupancy;
            }

            revision = next_revision(revision);
        }

        m_count.store(occupancy, std::memory_order_relaxed);
        update_high_water(occupancy);
        m_published_samples.fetch_add(count, std::memory_order_relaxed);
        m_overwritten_samples.fetch_add(overwritten, std::memory_order_relaxed);
        m_revision.store(revision, std::memory_order_release);
        record_producer_wait(wait_ns);
        lock.unlock();
    }

    // -------------------------------------------------------------------------
    // Consumer API (render thread) - thread-safe copy under lock
    // -------------------------------------------------------------------------

    /// Get a zero-copy view of current samples.
    /// Holds a shared lock for the lifetime of the returned View_result.
    View_result view() const {
        View_result view;
        view.lock = std::make_shared<std::shared_lock<std::shared_mutex>>(m_mutex);

        const std::size_t t = m_tail.load(std::memory_order_acquire);
        const std::size_t count = m_count.load(std::memory_order_acquire);
        view.sequence = m_revision.load(std::memory_order_acquire);

        if (count == 0) {
            return view;
        }

        const std::size_t cap = m_buffer.size();
        view.data = m_buffer.data() + t;
        view.count = count;
        const std::size_t first_count = std::min(count, cap - t);
        if (first_count < count) {
            view.data2 = m_buffer.data();
            view.count2 = count - first_count;
        }
        return view;
    }

    /// Copy all valid samples to destination vector.
    /// Holds shared lock during entire copy operation - THREAD SAFE.
    /// @param dest Destination vector (resized as needed)
    /// @return Copy_result with count and sequence
    Copy_result copy_to(std::vector<T>& dest) const {
        VNM_PLOT_PROFILE_SCOPE(m_profiler, "renderer.frame.data_copy");
        std::shared_lock lock(m_mutex);

        const std::size_t t = m_tail.load(std::memory_order_acquire);
        const std::size_t count = m_count.load(std::memory_order_acquire);
        const uint64_t revision = m_revision.load(std::memory_order_acquire);

        if (count == 0) {
            dest.clear();
            return {0, revision};
        }

        const std::size_t cap = m_buffer.size();
        const std::size_t first_count = std::min(count, cap - t);
        dest.resize(count);
        std::copy_n(m_buffer.begin() + t, first_count, dest.begin());
        if (first_count < count) {
            std::copy_n(m_buffer.begin(), count - first_count, dest.begin() + first_count);
        }

        return {count, revision};
    }

    /// Query current sequence without copying (for change detection).
    /// Returns sequence number which increments on every push.
    uint64_t sequence() const {
        return m_revision.load(std::memory_order_acquire);
    }

    // -------------------------------------------------------------------------
    // Query API
    // -------------------------------------------------------------------------

    /// Return the maximum capacity of the buffer.
    std::size_t capacity() const {
        return m_buffer.size();
    }

    /// Return current number of valid samples.
    /// Note: This is a snapshot and may change immediately after return.
    std::size_t size() const {
        return m_count.load(std::memory_order_acquire);
    }

    /// Check if buffer is empty.
    bool empty() const {
        return m_count.load(std::memory_order_acquire) == 0;
    }

    /// Clear all data while advancing the stable revision.
    void clear() {
        std::unique_lock lock(m_mutex);
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
        m_count.store(0, std::memory_order_relaxed);
        publish_next_revision();
    }

    Statistics statistics() const {
        Statistics stats;
        stats.occupancy = m_count.load(std::memory_order_acquire);
        stats.high_water_occupancy = m_high_water_occupancy.load(std::memory_order_acquire);
        stats.revision = m_revision.load(std::memory_order_acquire);
        stats.published_samples = m_published_samples.load(std::memory_order_acquire);
        stats.overwritten_samples = m_overwritten_samples.load(std::memory_order_acquire);
        stats.producer_wait_count = m_producer_wait_count.load(std::memory_order_acquire);
        stats.producer_wait_total_ns = m_producer_wait_total_ns.load(std::memory_order_acquire);
        const std::uint64_t wait_min = m_producer_wait_min_ns.load(std::memory_order_acquire);
        stats.producer_wait_min_ns = wait_min == std::numeric_limits<std::uint64_t>::max()
            ? 0
            : wait_min;
        stats.producer_wait_max_ns = m_producer_wait_max_ns.load(std::memory_order_acquire);
        return stats;
    }

    /// Start a measurement interval without changing content or revision.
    void reset_measurement_statistics()
    {
        std::unique_lock lock(m_mutex);
        const std::size_t occupancy = m_count.load(std::memory_order_relaxed);
        m_high_water_occupancy.store(occupancy, std::memory_order_relaxed);
        m_published_samples.store(0, std::memory_order_relaxed);
        m_overwritten_samples.store(0, std::memory_order_relaxed);
        m_producer_wait_count.store(0, std::memory_order_relaxed);
        m_producer_wait_total_ns.store(0, std::memory_order_relaxed);
        m_producer_wait_min_ns.store(
            std::numeric_limits<std::uint64_t>::max(),
            std::memory_order_relaxed);
        m_producer_wait_max_ns.store(0, std::memory_order_relaxed);
    }

private:
    static std::uint64_t next_revision(std::uint64_t revision) {
        return revision == std::numeric_limits<std::uint64_t>::max()
            ? 1
            : revision + 1;
    }

    void publish_next_revision() {
        const std::uint64_t revision = m_revision.load(std::memory_order_relaxed);
        m_revision.store(next_revision(revision), std::memory_order_release);
    }

    void update_high_water(std::size_t occupancy) {
        std::size_t previous = m_high_water_occupancy.load(std::memory_order_relaxed);
        while (previous < occupancy && !m_high_water_occupancy.compare_exchange_weak(
            previous,
            occupancy,
            std::memory_order_relaxed))
        {}
    }

    static double producer_wait_ns(std::chrono::steady_clock::time_point wait_started) {
        return std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - wait_started).count();
    }

    void record_producer_wait(double wait_ns) {
        const std::uint64_t value = wait_ns <= 0.0
            ? 0
            : static_cast<std::uint64_t>(std::ceil(wait_ns));
        m_producer_wait_count.fetch_add(1, std::memory_order_relaxed);
        m_producer_wait_total_ns.fetch_add(value, std::memory_order_relaxed);

        std::uint64_t previous_min = m_producer_wait_min_ns.load(std::memory_order_relaxed);
        while (value < previous_min && !m_producer_wait_min_ns.compare_exchange_weak(
            previous_min,
            value,
            std::memory_order_relaxed))
        {}

        std::uint64_t previous_max = m_producer_wait_max_ns.load(std::memory_order_relaxed);
        while (value > previous_max && !m_producer_wait_max_ns.compare_exchange_weak(
            previous_max,
            value,
            std::memory_order_relaxed))
        {}
    }

    std::vector<T> m_buffer;
    std::atomic<std::size_t> m_head{0};   ///< Write position (next slot to write)
    std::atomic<std::size_t> m_tail{0};   ///< Start of valid data
    std::atomic<std::size_t> m_count{0};
    std::atomic<std::size_t> m_high_water_occupancy{0};
    std::atomic<std::uint64_t> m_revision{1};
    std::atomic<std::uint64_t> m_published_samples{0};
    std::atomic<std::uint64_t> m_overwritten_samples{0};
    std::atomic<std::uint64_t> m_producer_wait_count{0};
    std::atomic<std::uint64_t> m_producer_wait_total_ns{0};
    std::atomic<std::uint64_t> m_producer_wait_min_ns{
        std::numeric_limits<std::uint64_t>::max()};
    std::atomic<std::uint64_t> m_producer_wait_max_ns{0};
    mutable std::shared_mutex m_mutex;    ///< Protects m_buffer during copy
    vnm::plot::Profiler* m_profiler = nullptr;
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_RING_BUFFER_H
