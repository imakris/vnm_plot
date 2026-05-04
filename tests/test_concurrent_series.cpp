// vnm_plot concurrent data-source tests

#include "test_macros.h"

#include <vnm_plot/core/types.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace plot = vnm::plot;

namespace {

struct sample_t
{
    double t = 0.0;
    float v = 0.0f;
};

bool test_vector_source_set_data_bumps_sequence()
{
    plot::Vector_data_source<sample_t> source;
    TEST_ASSERT(source.sequence() == 0, "initial sequence should be 0");

    source.set_data({{0.0, 0.0f}, {1.0, 1.0f}});
    TEST_ASSERT(source.sequence() == 1, "set_data should bump sequence once");

    source.notify_changed();
    TEST_ASSERT(source.sequence() == 2, "notify_changed should bump sequence");

    auto snap = source.try_snapshot(0);
    TEST_ASSERT(static_cast<bool>(snap), "snapshot should be READY when data is present");
    TEST_ASSERT(snap.snapshot.sequence == 2,
        "snapshot sequence should match Vector_data_source::sequence()");
    TEST_ASSERT(snap.snapshot.count == 2, "snapshot count should reflect stored samples");
    TEST_ASSERT(snap.snapshot.stride == sizeof(sample_t), "stride should be sizeof(sample_t)");
    return true;
}

bool test_vector_source_empty_snapshot_reports_empty_status()
{
    plot::Vector_data_source<sample_t> source;
    auto snap = source.try_snapshot(0);
    TEST_ASSERT(snap.status == plot::snapshot_result_t::Snapshot_status::EMPTY,
        "empty source should yield EMPTY status");
    TEST_ASSERT(!static_cast<bool>(snap), "snapshot_result_t with EMPTY status should be falsey");
    return true;
}

class Ring_source final : public plot::Data_source
{
public:
    plot::snapshot_result_t try_snapshot(std::size_t /*lod*/) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_samples.empty()) {
            return {plot::data_snapshot_t{}, plot::snapshot_result_t::Snapshot_status::EMPTY};
        }
        auto buffer = std::make_shared<std::vector<sample_t>>(m_samples);
        plot::data_snapshot_t snapshot;
        snapshot.data = buffer->data();
        snapshot.count = buffer->size();
        snapshot.stride = sizeof(sample_t);
        snapshot.sequence = m_sequence;
        snapshot.hold = buffer;
        return {snapshot, plot::snapshot_result_t::Snapshot_status::READY};
    }

    std::size_t sample_stride() const override { return sizeof(sample_t); }
    std::uint64_t current_sequence(std::size_t) const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sequence;
    }

    void append(const sample_t& s)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_samples.push_back(s);
        ++m_sequence;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<sample_t> m_samples;
    std::uint64_t m_sequence = 0;
};

bool test_ring_source_snapshots_are_consistent_under_concurrent_writes()
{
    Ring_source source;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> observed_max_sequence{0};
    std::atomic<std::uint64_t> reader_errors{0};

    std::thread writer([&] {
        for (std::size_t i = 0; i < 5000 && !stop.load(std::memory_order_acquire); ++i) {
            source.append({static_cast<double>(i), static_cast<float>(i)});
        }
    });

    auto reader_fn = [&] {
        while (!stop.load(std::memory_order_acquire)) {
            auto result = source.try_snapshot(0);
            if (!result) {
                continue;
            }
            const auto& snap = result.snapshot;
            const auto* first = reinterpret_cast<const sample_t*>(snap.data);
            double prev = first->t;
            for (std::size_t i = 1; i < snap.count; ++i) {
                const auto* cur = reinterpret_cast<const sample_t*>(
                    reinterpret_cast<const std::uint8_t*>(snap.data) + i * snap.stride);
                if (cur->t < prev) {
                    reader_errors.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                prev = cur->t;
            }
            std::uint64_t current = observed_max_sequence.load(std::memory_order_relaxed);
            while (snap.sequence > current &&
                   !observed_max_sequence.compare_exchange_weak(current, snap.sequence,
                       std::memory_order_relaxed)) {
            }
        }
    };

    std::thread reader1(reader_fn);
    std::thread reader2(reader_fn);

    writer.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.store(true, std::memory_order_release);
    reader1.join();
    reader2.join();

    TEST_ASSERT(reader_errors.load() == 0,
        "readers should never observe non-monotonic timestamps inside a snapshot");
    TEST_ASSERT(observed_max_sequence.load() > 0,
        "readers should observe at least one non-zero sequence");
    return true;
}

bool test_data_source_default_query_v_range_returns_false()
{
    Ring_source source;
    float v_min = 123.f;
    float v_max = 456.f;
    std::uint64_t seq = 999;
    // Query in int64 nanoseconds (API convention).
    const bool handled = source.query_v_range_for_t_window(
        std::int64_t{0}, std::int64_t{1}, v_min, v_max, &seq);
    TEST_ASSERT(!handled, "default Data_source should report v-range query as unsupported");
    TEST_ASSERT(v_min == 123.f && v_max == 456.f,
        "default query_v_range_for_t_window must not touch its outputs");
    TEST_ASSERT(seq == 999, "default query_v_range_for_t_window must not touch out_sequence");
    return true;
}

} // namespace

int main()
{
    std::cout << "Concurrent series / Data_source tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_vector_source_set_data_bumps_sequence);
    RUN_TEST(test_vector_source_empty_snapshot_reports_empty_status);
    RUN_TEST(test_ring_source_snapshots_are_consistent_under_concurrent_writes);
    RUN_TEST(test_data_source_default_query_v_range_returns_false);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
