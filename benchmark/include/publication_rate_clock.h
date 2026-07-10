// vnm_plot Benchmark - Pause-aware publication rate schedule

#ifndef VNM_PLOT_BENCHMARK_PUBLICATION_RATE_CLOCK_H
#define VNM_PLOT_BENCHMARK_PUBLICATION_RATE_CLOCK_H

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace vnm::benchmark {

class Publication_rate_clock {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    explicit Publication_rate_clock(time_point started_at = clock::now()) noexcept
        : m_started_at(started_at)
    {
    }

    void exclude_pause(duration paused) noexcept
    {
        m_started_at += paused;
    }

    std::size_t target_samples(time_point now, double samples_per_second) const noexcept
    {
        const double elapsed_seconds = std::max(
            0.0,
            std::chrono::duration<double>(now - m_started_at).count());
        return static_cast<std::size_t>(elapsed_seconds * samples_per_second);
    }

private:
    time_point m_started_at;
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_PUBLICATION_RATE_CLOCK_H
