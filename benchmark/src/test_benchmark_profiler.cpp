// vnm_plot Benchmark - Profiler Tests

#include "benchmark_profiler.h"
#include "allocation_tracker.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <thread>

using vnm::benchmark::Benchmark_profiler;
using vnm::benchmark::Profile_scope;
using vnm::benchmark::Report_metadata;

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
        } \
        else { \
            std::cout << "FAIL" << std::endl; \
            ++failed; \
        } \
    } while(0)

bool test_profiler_allocations_are_excluded_from_frame_measurement()
{
    Benchmark_profiler profiler;
    vnm::benchmark::begin_thread_allocation_measurement();
    for (int i = 0; i < 64; ++i) {
        profiler.begin_scope("instrumentation.scope");
        profiler.record_observation("instrumentation.observation", double(i));
        profiler.end_scope();
    }
    const auto measurement = vnm::benchmark::end_thread_allocation_measurement();
    TEST_ASSERT(measurement.count == 0,
        "profiler storage allocations must not contaminate frame allocation counts");
    TEST_ASSERT(measurement.bytes == 0,
        "profiler storage allocations must not contaminate frame allocation bytes");
    return true;
}

bool test_allocation_tracker_counts_ordinary_allocation()
{
    vnm::benchmark::begin_thread_allocation_measurement();
    void* memory = ::operator new(257);
    static_cast<volatile unsigned char*>(memory)[0] = 1;
    const auto measurement = vnm::benchmark::end_thread_allocation_measurement();
    ::operator delete(memory);
    TEST_ASSERT(measurement.count == 1,
        "allocation tracker positive control should count an ordinary allocation");
    TEST_ASSERT(measurement.bytes >= 257,
        "allocation tracker positive control should retain allocated bytes");
    return true;
}

bool test_allocation_failure_diagnostics()
{
    vnm::benchmark::clear_thread_allocation_failure();
    auto allocation_failure = vnm::benchmark::last_thread_allocation_failure();
    TEST_ASSERT(allocation_failure.size == 0, "failure size should reset");
    TEST_ASSERT(allocation_failure.alignment == 0, "failure alignment should reset");
    TEST_ASSERT(allocation_failure.error == 0, "failure error should reset");
    TEST_ASSERT(!allocation_failure.aligned, "failure aligned flag should reset");

    const std::size_t impossible_size = std::numeric_limits<std::size_t>::max();
    try {
        void* memory = ::operator new(impossible_size);
        ::operator delete(memory);
        TEST_ASSERT(false, "impossible allocation should throw std::bad_alloc");
    }
    catch (const std::bad_alloc&) {
    }
    allocation_failure = vnm::benchmark::last_thread_allocation_failure();
    TEST_ASSERT(allocation_failure.size == impossible_size,
        "failed allocation size should be retained");
    TEST_ASSERT(allocation_failure.alignment == 0,
        "ordinary failed allocation should not retain alignment");
    TEST_ASSERT(!allocation_failure.aligned,
        "ordinary failed allocation should not be marked aligned");
    vnm::benchmark::clear_thread_allocation_failure();
    return true;
}

// Test: Basic scope timing
bool test_basic_scope() {
    Benchmark_profiler profiler;

    profiler.begin_scope("test_scope");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    profiler.end_scope();

    TEST_ASSERT(profiler.root().children.count("test_scope") == 1, "scope should exist");

    const auto& scope = *profiler.root().children.at("test_scope");
    TEST_ASSERT(scope.call_count == 1, "call_count should be 1");
    TEST_ASSERT(scope.total_ms >= 9.0, "total_ms should be >= 9");  // Allow some tolerance
    TEST_ASSERT(scope.total_ms < 100.0, "total_ms should be < 100");

    return true;
}

std::string report_line_containing(const std::string& report, const std::string& needle) {
    std::istringstream iss(report);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find(needle) != std::string::npos) {
            return line;
        }
    }
    return {};
}

std::string line_after_report_line_containing(
    const std::string& report,
    const std::string& needle)
{
    std::istringstream iss(report);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find(needle) != std::string::npos) {
            std::string next_line;
            std::getline(iss, next_line);
            return next_line;
        }
    }
    return {};
}

double json_number_after(const std::string& json, const std::string& key) {
    const auto key_pos = json.find(key);
    if (key_pos == std::string::npos) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto value_pos = json.find_first_of("-0123456789", key_pos + key.size());
    if (value_pos == std::string::npos) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::stod(json.substr(value_pos));
}

// Test: Observation counters aggregate and report without millisecond units
bool test_observation_counters_are_unit_neutral() {
    Benchmark_profiler profiler;

    profiler.record_observation("renderer.frame.uploaded_sample_bytes", 1024.0);
    profiler.record_observation("renderer.frame.uploaded_sample_bytes", 2048.0);
    profiler.record_observation("renderer.frame.sample_upload_count", 2.0);
    profiler.record_observation("renderer.series_view.sample_upload_count", 1.0);
    profiler.record_counter("renderer.series_window.lod_switch_count");
    profiler.record_counter("renderer.series_window.lod_switch_count");
    profiler.record_counter("renderer.series_window.monotonicity_scan_count");
    profiler.record_observation_summary(
        "renderer.auto_range.query_count",
        3,
        3.0,
        1.0,
        1.0);
    profiler.record_counter("renderer.auto_range.range_scan_count");

    Report_metadata meta;
    meta.started_at = std::chrono::system_clock::now();
    meta.generated_at = std::chrono::system_clock::now();

    const std::string report = profiler.generate_report(meta);

    TEST_ASSERT(report.find("Observations:") != std::string::npos,
        "report should include observations section");
    const std::string header = report_line_containing(report, "Name");
    TEST_ASSERT(header.find("Total") != std::string::npos,
        "observation header should include unit-neutral Total column");
    TEST_ASSERT(header.find("Total ms") == std::string::npos,
        "observation header should not label counters as milliseconds");
    const std::string separator =
        line_after_report_line_containing(report, "Name");
    TEST_ASSERT(separator.length() == header.length(),
        "observation header and separator should match length");

    const std::string bytes_line =
        report_line_containing(report, "renderer.frame.uploaded_sample_bytes");
    TEST_ASSERT(bytes_line.find("    2") != std::string::npos,
        "uploaded sample bytes should aggregate two observations");
    TEST_ASSERT(bytes_line.find("3072.000") != std::string::npos,
        "uploaded sample bytes total should aggregate values");
    TEST_ASSERT(bytes_line.find("1536.000") != std::string::npos,
        "uploaded sample bytes average should aggregate values");
    TEST_ASSERT(bytes_line.find("1024.000") != std::string::npos,
        "uploaded sample bytes min should aggregate values");
    TEST_ASSERT(bytes_line.find("2048.000") != std::string::npos,
        "uploaded sample bytes max should aggregate values");

    const std::string upload_count_line =
        report_line_containing(report, "renderer.frame.sample_upload_count");
    TEST_ASSERT(upload_count_line.find("2.000") != std::string::npos,
        "frame sample upload count should be reported");

    const std::string series_view_upload_line =
        report_line_containing(report, "renderer.series_view.sample_upload_count");
    TEST_ASSERT(series_view_upload_line.find("1.000") != std::string::npos,
        "series/view sample upload count should be reported");

    const std::string lod_line =
        report_line_containing(report, "renderer.series_window.lod_switch_count");
    TEST_ASSERT(lod_line.find("    2") != std::string::npos,
        "LOD switch counter should aggregate two events");
    TEST_ASSERT(lod_line.find("2.000") != std::string::npos,
        "LOD switch counter total should be reported");

    const std::string monotonicity_line =
        report_line_containing(report, "renderer.series_window.monotonicity_scan_count");
    TEST_ASSERT(monotonicity_line.find("1.000") != std::string::npos,
        "monotonicity scan counter should be reported");

    const std::string query_line =
        report_line_containing(report, "renderer.auto_range.query_count");
    TEST_ASSERT(query_line.find("    3") != std::string::npos,
        "range query summary should preserve call count");
    TEST_ASSERT(query_line.find("3.000") != std::string::npos,
        "range query summary should preserve total");

    const std::string scan_line =
        report_line_containing(report, "renderer.auto_range.range_scan_count");
    TEST_ASSERT(scan_line.find("1.000") != std::string::npos,
        "range scan counter should be reported");

    return true;
}

// Test: Raw artifact retains samples and calculated percentile fields
bool test_raw_report_retains_samples() {
    Benchmark_profiler profiler;
    profiler.record_observation("benchmark.frame.total_ms", 1.0);
    profiler.record_observation("benchmark.frame.total_ms", 2.0);
    profiler.record_observation("benchmark.frame.total_ms", 9.0);

    Report_metadata meta;
    meta.session = "raw_test";
    meta.stream = "TEST";
    meta.output_directory = std::filesystem::temp_directory_path() /
        "vnm_plot_benchmark_profiler_test";
    meta.started_at = std::chrono::system_clock::from_time_t(1'700'000'000);
    meta.generated_at = meta.started_at;
    meta.reproduction["scenario"] = "unit-test";

    const auto raw_path = profiler.write_raw_report(meta);
    std::ifstream input(raw_path);
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();

    TEST_ASSERT(!json.empty(), "raw report should be written");
    TEST_ASSERT(json.find("\"scenario\": \"unit-test\"") != std::string::npos,
        "raw report should include reproduction metadata");
    TEST_ASSERT(json.find("\"p50\": 2") != std::string::npos,
        "raw report should calculate p50");
    TEST_ASSERT(json.find("\"p95\": 8.3") != std::string::npos,
        "raw report should calculate interpolated p95");
    TEST_ASSERT(json.find("\"samples\": [1, 2, 9]") != std::string::npos,
        "raw report should retain observation samples");

    std::error_code ec;
    std::filesystem::remove_all(meta.output_directory, ec);
    return true;
}

// Test: Bounded Algorithm R retention remains representative and deterministic
bool test_reservoir_sampling_is_representative() {
    Benchmark_profiler first;
    Benchmark_profiler second;
    for (int value = 0; value < 100'000; ++value) {
        first.record_observation("monotonic", static_cast<double>(value));
        second.record_observation("monotonic", static_cast<double>(value));
    }

    Report_metadata meta;
    meta.stream = "RESERVOIR";
    meta.output_directory = std::filesystem::temp_directory_path() /
        "vnm_plot_benchmark_reservoir_test";
    meta.started_at = std::chrono::system_clock::from_time_t(1'700'000'001);
    meta.generated_at = meta.started_at;

    const auto first_path = first.write_raw_report(meta);
    std::ifstream first_input(first_path);
    std::ostringstream first_contents;
    first_contents << first_input.rdbuf();

    meta.stream = "RESERVOIR2";
    const auto second_path = second.write_raw_report(meta);
    std::ifstream second_input(second_path);
    std::ostringstream second_contents;
    second_contents << second_input.rdbuf();

    const double p50 = json_number_after(first_contents.str(), "\"p50\":");
    TEST_ASSERT(p50 > 48'000.0 && p50 < 52'000.0,
        "reservoir p50 should represent the full monotonic stream");
    const double second_p50 = json_number_after(second_contents.str(), "\"p50\":");
    TEST_ASSERT(p50 == second_p50,
        "reservoir sampling should be deterministic for the same metric and stream");
    TEST_ASSERT(first_contents.str().find("\"retained_sample_count\": 8192") !=
            std::string::npos,
        "reservoir should reach its full retained-sample capacity");

    std::error_code ec;
    std::filesystem::remove_all(meta.output_directory, ec);
    return true;
}

// Test: Nested scopes
bool test_nested_scopes() {
    Benchmark_profiler profiler;

    profiler.begin_scope("parent");
    profiler.begin_scope("child");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    profiler.end_scope();  // child
    profiler.end_scope();  // parent

    TEST_ASSERT(profiler.root().children.count("parent") == 1, "parent should exist");

    const auto& parent = *profiler.root().children.at("parent");
    TEST_ASSERT(parent.children.count("child") == 1, "child should exist");
    TEST_ASSERT(parent.call_count == 1, "parent call_count should be 1");

    const auto& child = *parent.children.at("child");
    TEST_ASSERT(child.call_count == 1, "child call_count should be 1");

    return true;
}

// Test: Scope aggregation (same name multiple times)
bool test_scope_aggregation() {
    Benchmark_profiler profiler;

    for (int i = 0; i < 5; ++i) {
        profiler.begin_scope("repeated");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        profiler.end_scope();
    }

    TEST_ASSERT(profiler.root().children.size() == 1, "should have only one child");

    const auto& scope = *profiler.root().children.at("repeated");
    TEST_ASSERT(scope.call_count == 5, "call_count should be 5");
    TEST_ASSERT(scope.total_ms >= 8.0, "total_ms should be >= 8");

    return true;
}

// Test: Min/max tracking
bool test_min_max() {
    Benchmark_profiler profiler;

    // First call - short
    profiler.begin_scope("varied");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    profiler.end_scope();

    // Second call - longer
    profiler.begin_scope("varied");
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    profiler.end_scope();

    const auto& scope = *profiler.root().children.at("varied");
    TEST_ASSERT(scope.call_count == 2, "call_count should be 2");
    TEST_ASSERT(scope.min_ms < scope.max_ms, "min should be < max");
    TEST_ASSERT(scope.min_ms >= 4.0, "min should be >= 4");
    TEST_ASSERT(scope.max_ms >= 14.0, "max should be >= 14");

    return true;
}

// Test: Reset functionality
bool test_reset() {
    Benchmark_profiler profiler;

    profiler.begin_scope("test");
    profiler.end_scope();

    TEST_ASSERT(!profiler.root().children.empty(), "should have children before reset");

    profiler.reset();

    TEST_ASSERT(profiler.root().children.empty(), "should have no children after reset");

    return true;
}

// Test: RAII scope guard
bool test_raii_scope() {
    Benchmark_profiler profiler;

    {
        Profile_scope scope(profiler, "raii_test");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    TEST_ASSERT(profiler.root().children.count("raii_test") == 1, "raii scope should exist");
    TEST_ASSERT(profiler.root().children.at("raii_test")->call_count == 1, "call_count should be 1");

    return true;
}

// Test: Report generation structure
bool test_report_generation() {
    Benchmark_profiler profiler;

    profiler.begin_scope("outer");
    profiler.begin_scope("inner");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    profiler.end_scope();
    profiler.end_scope();

    Report_metadata meta;
    meta.session = "test_session";
    meta.stream = "TEST";
    meta.data_type = "Bars";
    meta.target_duration = 10.0;
    meta.started_at = std::chrono::system_clock::now();
    meta.generated_at = std::chrono::system_clock::now();

    std::string report = profiler.generate_report(meta);

    // Check header elements
    TEST_ASSERT(report.find("vnm_plot profiling report") != std::string::npos, "should have header");
    TEST_ASSERT(report.find("Session: test_session") != std::string::npos, "should have session");
    TEST_ASSERT(report.find("stream: TEST") != std::string::npos, "should have stream");

    // Check table structure
    TEST_ASSERT(report.find("Section") != std::string::npos, "should have Section column");
    TEST_ASSERT(report.find("Calls") != std::string::npos, "should have Calls column");
    TEST_ASSERT(report.find("Total ms") != std::string::npos, "should have Total ms column");

    // Check scope names in output
    TEST_ASSERT(report.find("outer") != std::string::npos, "should have outer scope");
    TEST_ASSERT(report.find("inner") != std::string::npos, "should have inner scope");

    return true;
}

// Test: Extended metadata
bool test_extended_metadata() {
    Benchmark_profiler profiler;

    profiler.begin_scope("test");
    profiler.end_scope();

    Report_metadata meta;
    meta.include_extended = true;
    meta.seed = 12345;
    meta.volatility = 0.02;
    meta.ring_capacity = 100000;
    meta.samples_generated = 50000;
    meta.started_at = std::chrono::system_clock::now();
    meta.generated_at = std::chrono::system_clock::now();

    std::string report = profiler.generate_report(meta);

    TEST_ASSERT(report.find("seed: 12345") != std::string::npos, "should have seed");
    TEST_ASSERT(report.find("volatility:") != std::string::npos, "should have volatility");
    TEST_ASSERT(report.find("ring_capacity: 100000") != std::string::npos, "should have ring_capacity");

    return true;
}

// Test: Deep nesting
bool test_deep_nesting() {
    Benchmark_profiler profiler;

    profiler.begin_scope("level1");
    profiler.begin_scope("level2");
    profiler.begin_scope("level3");
    profiler.begin_scope("level4");
    profiler.end_scope();
    profiler.end_scope();
    profiler.end_scope();
    profiler.end_scope();

    const auto& l1 = *profiler.root().children.at("level1");
    TEST_ASSERT(l1.children.count("level2") == 1, "level2 should exist");

    const auto& l2 = *l1.children.at("level2");
    TEST_ASSERT(l2.children.count("level3") == 1, "level3 should exist");

    const auto& l3 = *l2.children.at("level3");
    TEST_ASSERT(l3.children.count("level4") == 1, "level4 should exist");

    return true;
}

// Test: Multiple top-level scopes
bool test_multiple_roots() {
    Benchmark_profiler profiler;

    profiler.begin_scope("first");
    profiler.end_scope();

    profiler.begin_scope("second");
    profiler.end_scope();

    profiler.begin_scope("third");
    profiler.end_scope();

    TEST_ASSERT(profiler.root().children.size() == 3, "should have 3 top-level scopes");
    TEST_ASSERT(profiler.root().children.count("first") == 1, "first should exist");
    TEST_ASSERT(profiler.root().children.count("second") == 1, "second should exist");
    TEST_ASSERT(profiler.root().children.count("third") == 1, "third should exist");

    return true;
}

// Test: Format compliance (header and separator lengths)
bool test_format_compliance() {
    Benchmark_profiler profiler;

    profiler.begin_scope("test");
    profiler.end_scope();

    Report_metadata meta;
    meta.started_at = std::chrono::system_clock::now();
    meta.generated_at = std::chrono::system_clock::now();

    std::string report = profiler.generate_report(meta);

    // Find the table header and separator lines
    std::istringstream iss(report);
    std::string line;
    std::string header_line;
    std::string separator_line;

    while (std::getline(iss, line)) {
        if (line.find("Section") == 0 && line.find("Calls") != std::string::npos) {
            header_line = line;
        }
        else
        if (line.find("---") == 0 && line.length() > 50) {
            separator_line = line;
        }
    }

    // Both lines should be 108 characters per benchmark format spec
    TEST_ASSERT(!header_line.empty(), "header line should exist");
    TEST_ASSERT(!separator_line.empty(), "separator line should exist");
    TEST_ASSERT(header_line.length() == 108, "header line should be 108 chars");
    TEST_ASSERT(separator_line.length() == 108, "separator line should be 108 chars");
    TEST_ASSERT(header_line.length() == separator_line.length(), "header and separator should match length");

    return true;
}

int main() {
    std::cout << "Benchmark Profiler Test Suite\n";
    std::cout << "=============================\n\n";

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_basic_scope);
    RUN_TEST(test_allocation_tracker_counts_ordinary_allocation);
    RUN_TEST(test_allocation_failure_diagnostics);
    RUN_TEST(test_profiler_allocations_are_excluded_from_frame_measurement);
    RUN_TEST(test_nested_scopes);
    RUN_TEST(test_scope_aggregation);
    RUN_TEST(test_min_max);
    RUN_TEST(test_reset);
    RUN_TEST(test_raii_scope);
    RUN_TEST(test_report_generation);
    RUN_TEST(test_extended_metadata);
    RUN_TEST(test_deep_nesting);
    RUN_TEST(test_multiple_roots);
    RUN_TEST(test_format_compliance);
    RUN_TEST(test_observation_counters_are_unit_neutral);
    RUN_TEST(test_raw_report_retains_samples);
    RUN_TEST(test_reservoir_sampling_is_representative);

    std::cout << "\n=============================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    return failed > 0 ? 1 : 0;
}
