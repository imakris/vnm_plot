// vnm_plot Benchmark - Render-thread CPU allocation tracking

#ifndef VNM_PLOT_BENCHMARK_ALLOCATION_TRACKER_H
#define VNM_PLOT_BENCHMARK_ALLOCATION_TRACKER_H

#include <cstddef>
#include <cstdint>

namespace vnm::benchmark {

struct Thread_allocation_measurement {
    std::uint64_t count = 0;
    std::uint64_t bytes = 0;
};

struct Thread_allocation_failure {
    std::size_t size = 0;
    std::size_t alignment = 0;
    int error = 0;
    bool aligned = false;
};

void begin_thread_allocation_measurement() noexcept;
Thread_allocation_measurement end_thread_allocation_measurement() noexcept;
void suspend_thread_allocation_measurement() noexcept;
void resume_thread_allocation_measurement() noexcept;
void clear_thread_allocation_failure() noexcept;
Thread_allocation_failure last_thread_allocation_failure() noexcept;

class Thread_allocation_suppression {
public:
    Thread_allocation_suppression() noexcept
    {
        suspend_thread_allocation_measurement();
    }

    ~Thread_allocation_suppression()
    {
        resume_thread_allocation_measurement();
    }

    Thread_allocation_suppression(const Thread_allocation_suppression&) = delete;
    Thread_allocation_suppression& operator=(const Thread_allocation_suppression&) = delete;
};

class Thread_allocation_scope {
public:
    explicit Thread_allocation_scope(bool enabled) noexcept
        : m_enabled(enabled)
    {
        if (m_enabled) {
            begin_thread_allocation_measurement();
        }
    }

    ~Thread_allocation_scope()
    {
        if (m_enabled) {
            (void)end_thread_allocation_measurement();
        }
    }

    Thread_allocation_scope(const Thread_allocation_scope&) = delete;
    Thread_allocation_scope& operator=(const Thread_allocation_scope&) = delete;

    Thread_allocation_measurement finish() noexcept
    {
        if (!m_enabled) {
            return {};
        }
        m_enabled = false;
        return end_thread_allocation_measurement();
    }

private:
    bool m_enabled;
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_ALLOCATION_TRACKER_H
