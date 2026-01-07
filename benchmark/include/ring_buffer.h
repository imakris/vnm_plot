// vnm_plot Benchmark - Ring Buffer
// Thread-safe, fixed-capacity circular buffer for streaming data

#ifndef VNM_PLOT_BENCHMARK_RING_BUFFER_H
#define VNM_PLOT_BENCHMARK_RING_BUFFER_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
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
    /// Result of a copy operation
    struct Copy_result {
        std::size_t count;    ///< Number of samples copied
        uint64_t sequence;    ///< Sequence at time of copy
    };

    /// Construct a ring buffer with the given capacity.
    /// @param capacity Maximum number of elements (must be > 0)
    explicit Ring_buffer(std::size_t capacity)
        : buffer_(capacity)
    {
        // Capacity of 0 would cause division by zero
        if (capacity == 0) {
            buffer_.resize(1);
        }
    }

    // Non-copyable, non-movable (mutex not movable)
    Ring_buffer(const Ring_buffer&) = delete;
    Ring_buffer& operator=(const Ring_buffer&) = delete;
    Ring_buffer(Ring_buffer&&) = delete;
    Ring_buffer& operator=(Ring_buffer&&) = delete;

    // -------------------------------------------------------------------------
    // Producer API (data generator thread)
    // -------------------------------------------------------------------------

    /// Push a single sample. Overwrites oldest if full.
    void push(const T& sample) {
        std::unique_lock lock(mutex_);

        const std::size_t cap = buffer_.size();
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const uint64_t seq = sequence_.load(std::memory_order_relaxed);

        // Check if buffer is currently full (head == tail && has data)
        const bool was_full = (h == t) && (seq > 0);

        buffer_[h] = sample;
        head_.store((h + 1) % cap, std::memory_order_relaxed);

        // If buffer was full, advance tail to discard oldest
        if (was_full) {
            tail_.store((t + 1) % cap, std::memory_order_relaxed);
        }

        sequence_.fetch_add(1, std::memory_order_release);
    }

    /// Push multiple samples. Overwrites oldest as needed.
    void push_batch(const T* samples, std::size_t count) {
        if (count == 0 || samples == nullptr) return;

        std::unique_lock lock(mutex_);

        const std::size_t cap = buffer_.size();

        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t h = head_.load(std::memory_order_relaxed);
            const std::size_t t = tail_.load(std::memory_order_relaxed);
            const uint64_t seq = sequence_.load(std::memory_order_relaxed);

            // Check if buffer is currently full (head == tail && has data)
            const bool was_full = (h == t) && (seq > 0);

            buffer_[h] = samples[i];
            head_.store((h + 1) % cap, std::memory_order_relaxed);

            // If buffer was full, advance tail to discard oldest
            if (was_full) {
                tail_.store((t + 1) % cap, std::memory_order_relaxed);
            }

            sequence_.fetch_add(1, std::memory_order_relaxed);
        }

        // Release fence for change-detection consistency
        std::atomic_thread_fence(std::memory_order_release);
    }

    // -------------------------------------------------------------------------
    // Consumer API (render thread) - thread-safe copy under lock
    // -------------------------------------------------------------------------

    /// Copy all valid samples to destination vector.
    /// Holds shared lock during entire copy operation - THREAD SAFE.
    /// @param dest Destination vector (resized as needed)
    /// @return Copy_result with count and sequence
    Copy_result copy_to(std::vector<T>& dest) const {
        std::shared_lock lock(mutex_);

        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        const uint64_t seq = sequence_.load(std::memory_order_acquire);

        if (h == t && seq == 0) {
            // Empty buffer (never written to)
            dest.clear();
            return {0, seq};
        }

        const std::size_t cap = buffer_.size();
        std::size_t count;

        if (h > t) {
            // Contiguous: [tail, head)
            count = h - t;
            dest.resize(count);
            std::copy(buffer_.begin() + t, buffer_.begin() + h, dest.begin());
        } else if (h < t) {
            // Wrapped: [tail, cap) + [0, head)
            const std::size_t part1 = cap - t;
            const std::size_t part2 = h;
            count = part1 + part2;
            dest.resize(count);
            std::copy(buffer_.begin() + t, buffer_.end(), dest.begin());
            std::copy(buffer_.begin(), buffer_.begin() + h, dest.begin() + part1);
        } else {
            // h == t && seq > 0: buffer is full (all N elements valid)
            count = cap;
            dest.resize(count);
            // Copy from tail to end, then from beginning to tail
            std::copy(buffer_.begin() + t, buffer_.end(), dest.begin());
            std::copy(buffer_.begin(), buffer_.begin() + t, dest.begin() + (cap - t));
        }

        return {count, seq};
    }

    /// Query current sequence without copying (for change detection).
    /// Returns sequence number which increments on every push.
    uint64_t sequence() const {
        return sequence_.load(std::memory_order_acquire);
    }

    // -------------------------------------------------------------------------
    // Query API
    // -------------------------------------------------------------------------

    /// Return the maximum capacity of the buffer.
    std::size_t capacity() const {
        return buffer_.size();
    }

    /// Return current number of valid samples.
    /// Note: This is a snapshot and may change immediately after return.
    std::size_t size() const {
        std::shared_lock lock(mutex_);
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        const uint64_t seq = sequence_.load(std::memory_order_acquire);

        if (h == t) {
            return seq == 0 ? 0 : buffer_.size();  // Empty or full
        }
        if (h > t) {
            return h - t;
        }
        return buffer_.size() - t + h;
    }

    /// Check if buffer is empty.
    bool empty() const {
        return sequence_.load(std::memory_order_acquire) == 0;
    }

    /// Clear all data and reset sequence.
    void clear() {
        std::unique_lock lock(mutex_);
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        sequence_.store(0, std::memory_order_release);
    }

private:
    std::vector<T> buffer_;
    std::atomic<std::size_t> head_{0};   ///< Write position (next slot to write)
    std::atomic<std::size_t> tail_{0};   ///< Start of valid data
    std::atomic<uint64_t> sequence_{0};  ///< Increments on every push
    mutable std::shared_mutex mutex_;    ///< Protects buffer_ during copy
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_RING_BUFFER_H
