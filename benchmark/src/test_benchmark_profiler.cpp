// vnm_plot Benchmark - Profiler Tests

#include "benchmark_profiler.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

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
        } else if (line.find("---") == 0 && line.length() > 50) {
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

    std::cout << "\n=============================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    return failed > 0 ? 1 : 0;
}
