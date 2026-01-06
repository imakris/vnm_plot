// vnm_plot Benchmark - Data Source Tests

#include "benchmark_data_source.h"
#include "brownian_generator.h"

#include <iostream>
#include <thread>
#include <chrono>

using namespace vnm::benchmark;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define RUN_TEST(test_fn) \
    do { \
        std::cout << "Running " << #test_fn << "... "; \
        if (test_fn()) { \
            std::cout << "PASS" << std::endl; \
            ++passed; \
        } else { \
            std::cout << "FAIL" << std::endl; \
            ++failed; \
        } \
    } while(0)

// Test: Empty buffer returns EMPTY status and has_value_range is false
bool test_empty_buffer() {
    Ring_buffer<Bar_sample> buffer(100);
    Benchmark_data_source<Bar_sample> source(buffer);

    auto result = source.try_snapshot();

    TEST_ASSERT(result.status == vnm::plot::snapshot_result_t::Status::EMPTY,
                "empty buffer should return EMPTY status");
    TEST_ASSERT(result.snapshot.count == 0, "snapshot count should be 0");
    TEST_ASSERT(!source.has_value_range(), "empty buffer should not have valid range");
    TEST_ASSERT(source.value_range_needs_rescan(), "empty buffer should need rescan");

    return true;
}

// Test: Snapshot contains data after push
bool test_snapshot_with_data() {
    Ring_buffer<Bar_sample> buffer(100);
    Benchmark_data_source<Bar_sample> source(buffer);

    // Push some data
    Bar_sample bar{};
    bar.timestamp = 1.0;
    bar.open = 100.0f;
    bar.high = 105.0f;
    bar.low = 95.0f;
    bar.close = 102.0f;
    bar.volume = 1000.0f;
    buffer.push(bar);

    auto result = source.try_snapshot();

    TEST_ASSERT(result.status == vnm::plot::snapshot_result_t::Status::OK,
                "should return OK status");
    TEST_ASSERT(result.snapshot.count == 1, "snapshot count should be 1");
    TEST_ASSERT(result.snapshot.stride == sizeof(Bar_sample), "stride should match");
    TEST_ASSERT(result.snapshot.data != nullptr, "data should not be null");

    return true;
}

// Test: Snapshot data is correct
bool test_snapshot_data_correctness() {
    Ring_buffer<Bar_sample> buffer(100);
    Benchmark_data_source<Bar_sample> source(buffer);

    // Push specific data
    Bar_sample bar{};
    bar.timestamp = 123.456;
    bar.open = 100.0f;
    bar.high = 110.0f;
    bar.low = 90.0f;
    bar.close = 105.0f;
    bar.volume = 5000.0f;
    buffer.push(bar);

    source.try_snapshot();

    // Access through snapshot_data()
    const auto& data = source.snapshot_data();
    TEST_ASSERT(data.size() == 1, "should have 1 sample");
    TEST_ASSERT(data[0].timestamp == 123.456, "timestamp should match");
    TEST_ASSERT(data[0].close == 105.0f, "close should match");

    return true;
}

// Test: Value range computation for bars
bool test_bar_value_range() {
    Ring_buffer<Bar_sample> buffer(100);
    Benchmark_data_source<Bar_sample> source(buffer);

    // Push bars with known ranges
    Bar_sample bar1{};
    bar1.low = 90.0f;
    bar1.high = 100.0f;
    buffer.push(bar1);

    Bar_sample bar2{};
    bar2.low = 85.0f;  // New minimum
    bar2.high = 95.0f;
    buffer.push(bar2);

    Bar_sample bar3{};
    bar3.low = 92.0f;
    bar3.high = 110.0f;  // New maximum
    buffer.push(bar3);

    source.try_snapshot();

    auto [min_val, max_val] = source.value_range();
    TEST_ASSERT(min_val == 85.0f, "min should be 85.0");
    TEST_ASSERT(max_val == 110.0f, "max should be 110.0");
    TEST_ASSERT(source.has_value_range(), "should have value range");

    return true;
}

// Test: Value range for trades
bool test_trade_value_range() {
    Ring_buffer<Trade_sample> buffer(100);
    Benchmark_data_source<Trade_sample> source(buffer);

    Trade_sample t1{};
    t1.price = 100.0f;
    buffer.push(t1);

    Trade_sample t2{};
    t2.price = 90.0f;
    buffer.push(t2);

    Trade_sample t3{};
    t3.price = 110.0f;
    buffer.push(t3);

    source.try_snapshot();

    auto [min_val, max_val] = source.value_range();
    TEST_ASSERT(min_val == 90.0f, "min should be 90.0");
    TEST_ASSERT(max_val == 110.0f, "max should be 110.0");

    return true;
}

// Test: Sample stride
bool test_sample_stride() {
    Ring_buffer<Bar_sample> bar_buffer(100);
    Benchmark_data_source<Bar_sample> bar_source(bar_buffer);

    Ring_buffer<Trade_sample> trade_buffer(100);
    Benchmark_data_source<Trade_sample> trade_source(trade_buffer);

    TEST_ASSERT(bar_source.sample_stride() == 28, "Bar stride should be 28");
    TEST_ASSERT(trade_source.sample_stride() == 16, "Trade stride should be 16");

    return true;
}

// Test: Sequence tracking
bool test_sequence_tracking() {
    Ring_buffer<Bar_sample> buffer(100);
    Benchmark_data_source<Bar_sample> source(buffer);

    TEST_ASSERT(source.sequence() == 0, "initial sequence should be 0");

    Bar_sample bar{};
    buffer.push(bar);
    source.try_snapshot();

    TEST_ASSERT(source.sequence() == 1, "sequence should be 1 after one push");

    buffer.push(bar);
    buffer.push(bar);
    source.try_snapshot();

    TEST_ASSERT(source.sequence() == 3, "sequence should be 3 after three pushes");

    return true;
}

// Test: Bar access policy
bool test_bar_access_policy() {
    auto policy = make_bar_access_policy();

    TEST_ASSERT(policy.is_valid(), "policy should be valid");
    TEST_ASSERT(policy.sample_stride == 28, "stride should be 28");

    Bar_sample bar{};
    bar.timestamp = 1000.0;
    bar.close = 105.5f;
    bar.low = 100.0f;
    bar.high = 110.0f;
    bar.volume = 5000.0f;

    TEST_ASSERT(policy.get_timestamp(&bar) == 1000.0, "timestamp extraction");
    TEST_ASSERT(policy.get_value(&bar) == 105.5f, "value extraction (close)");

    auto [lo, hi] = policy.get_range(&bar);
    TEST_ASSERT(lo == 100.0f, "range low");
    TEST_ASSERT(hi == 110.0f, "range high");

    TEST_ASSERT(policy.get_aux_metric(&bar) == 5000.0, "aux metric (volume)");

    return true;
}

// Test: Trade access policy
bool test_trade_access_policy() {
    auto policy = make_trade_access_policy();

    TEST_ASSERT(policy.is_valid(), "policy should be valid");
    TEST_ASSERT(policy.sample_stride == 16, "stride should be 16");

    Trade_sample trade{};
    trade.timestamp = 2000.0;
    trade.price = 99.5f;
    trade.size = 100.0f;

    TEST_ASSERT(policy.get_timestamp(&trade) == 2000.0, "timestamp extraction");
    TEST_ASSERT(policy.get_value(&trade) == 99.5f, "value extraction (price)");

    auto [lo, hi] = policy.get_range(&trade);
    TEST_ASSERT(lo == 99.5f, "range low == price");
    TEST_ASSERT(hi == 99.5f, "range high == price");

    TEST_ASSERT(policy.get_aux_metric(&trade) == 100.0, "aux metric (size)");

    return true;
}

// Test: Copy-on-snapshot isolation
bool test_copy_on_snapshot_isolation() {
    Ring_buffer<Bar_sample> buffer(100);
    Benchmark_data_source<Bar_sample> source(buffer);

    // Push initial data
    Bar_sample bar1{};
    bar1.close = 100.0f;
    buffer.push(bar1);

    // Take snapshot
    source.try_snapshot();
    float initial_close = source.snapshot_data()[0].close;

    // Modify buffer after snapshot
    Bar_sample bar2{};
    bar2.close = 200.0f;
    buffer.push(bar2);

    // Snapshot data should be unchanged until next try_snapshot
    TEST_ASSERT(source.snapshot_data()[0].close == initial_close,
                "snapshot should be isolated from buffer changes");

    // Now take new snapshot
    source.try_snapshot();
    TEST_ASSERT(source.snapshot_data().size() == 2, "new snapshot should have 2 samples");

    return true;
}

// Test: Integration with Brownian generator
bool test_brownian_integration() {
    Brownian_generator::Config config;
    config.seed = 42;
    Brownian_generator gen(config);

    Ring_buffer<Bar_sample> buffer(1000);
    Benchmark_data_source<Bar_sample> source(buffer);

    // Generate some bars
    for (int i = 0; i < 100; ++i) {
        buffer.push(gen.next_bar());
    }

    auto result = source.try_snapshot();
    TEST_ASSERT(result.status == vnm::plot::snapshot_result_t::Status::OK, "should be OK");
    TEST_ASSERT(result.snapshot.count == 100, "should have 100 samples");

    // Value range should be valid
    auto [min_val, max_val] = source.value_range();
    TEST_ASSERT(min_val < max_val, "min should be less than max");
    TEST_ASSERT(min_val > 0.0f, "min should be positive (price data)");

    return true;
}

// Test: Unsupported LOD levels return FAILED
bool test_unsupported_lod() {
    Ring_buffer<Bar_sample> buffer(100);
    Benchmark_data_source<Bar_sample> source(buffer);

    // Push some data first
    Bar_sample bar{};
    buffer.push(bar);

    // LOD 0 should work
    auto result0 = source.try_snapshot(0);
    TEST_ASSERT(result0.status == vnm::plot::snapshot_result_t::Status::OK,
                "LOD 0 should return OK");

    // LOD 1 should fail
    auto result1 = source.try_snapshot(1);
    TEST_ASSERT(result1.status == vnm::plot::snapshot_result_t::Status::FAILED,
                "LOD 1 should return FAILED");

    // LOD 100 should fail
    auto result100 = source.try_snapshot(100);
    TEST_ASSERT(result100.status == vnm::plot::snapshot_result_t::Status::FAILED,
                "LOD 100 should return FAILED");

    return true;
}

// Test: Sequence short-circuit avoids redundant copies
bool test_sequence_short_circuit() {
    Ring_buffer<Bar_sample> buffer(100);
    Benchmark_data_source<Bar_sample> source(buffer);

    // Push some data
    Bar_sample bar{};
    bar.close = 100.0f;
    buffer.push(bar);

    // First snapshot
    auto result1 = source.try_snapshot();
    TEST_ASSERT(result1.status == vnm::plot::snapshot_result_t::Status::OK,
                "first snapshot should be OK");
    uint64_t seq1 = source.sequence();

    // Second snapshot without any new data should return cached result
    auto result2 = source.try_snapshot();
    TEST_ASSERT(result2.status == vnm::plot::snapshot_result_t::Status::OK,
                "cached snapshot should be OK");
    TEST_ASSERT(result2.snapshot.count == result1.snapshot.count,
                "cached snapshot should have same count");
    TEST_ASSERT(result2.snapshot.data == result1.snapshot.data,
                "cached snapshot should return same pointer");

    // Push new data
    Bar_sample bar2{};
    bar2.close = 200.0f;
    buffer.push(bar2);

    // Third snapshot should detect change and copy new data
    auto result3 = source.try_snapshot();
    TEST_ASSERT(result3.status == vnm::plot::snapshot_result_t::Status::OK,
                "new snapshot should be OK");
    TEST_ASSERT(result3.snapshot.count == 2, "new snapshot should have 2 samples");
    TEST_ASSERT(source.sequence() > seq1, "sequence should have increased");

    return true;
}

// Test: Access policies have setup_vertex_attributes and layout_key
bool test_access_policy_vertex_setup() {
    auto bar_policy = make_bar_access_policy();
    auto trade_policy = make_trade_access_policy();

    // Check that setup_vertex_attributes is set
    TEST_ASSERT(bar_policy.setup_vertex_attributes != nullptr,
                "bar policy should have setup_vertex_attributes");
    TEST_ASSERT(trade_policy.setup_vertex_attributes != nullptr,
                "trade policy should have setup_vertex_attributes");

    // Check that layout_key is set and unique
    TEST_ASSERT(bar_policy.layout_key != 0, "bar policy should have non-zero layout_key");
    TEST_ASSERT(trade_policy.layout_key != 0, "trade policy should have non-zero layout_key");
    TEST_ASSERT(bar_policy.layout_key != trade_policy.layout_key,
                "bar and trade policies should have different layout_keys");

    // Verify expected layout key values
    TEST_ASSERT(bar_policy.layout_key == k_bar_sample_layout_key,
                "bar policy layout_key should match constant");
    TEST_ASSERT(trade_policy.layout_key == k_trade_sample_layout_key,
                "trade policy layout_key should match constant");

    return true;
}

int main() {
    std::cout << "Benchmark Data Source Test Suite\n";
    std::cout << "=================================\n\n";

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_empty_buffer);
    RUN_TEST(test_snapshot_with_data);
    RUN_TEST(test_snapshot_data_correctness);
    RUN_TEST(test_bar_value_range);
    RUN_TEST(test_trade_value_range);
    RUN_TEST(test_sample_stride);
    RUN_TEST(test_sequence_tracking);
    RUN_TEST(test_bar_access_policy);
    RUN_TEST(test_trade_access_policy);
    RUN_TEST(test_copy_on_snapshot_isolation);
    RUN_TEST(test_brownian_integration);
    RUN_TEST(test_unsupported_lod);
    RUN_TEST(test_sequence_short_circuit);
    RUN_TEST(test_access_policy_vertex_setup);

    std::cout << "\n=================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    return failed > 0 ? 1 : 0;
}
