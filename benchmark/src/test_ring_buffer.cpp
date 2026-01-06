// vnm_plot Benchmark - Ring Buffer Tests
// Simple test suite to verify Ring_buffer correctness

#include "ring_buffer.h"

#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace vnm::benchmark;

// Test helper macros
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

// Test: Empty buffer behavior
bool test_empty_buffer() {
    Ring_buffer<int> buf(10);

    TEST_ASSERT(buf.capacity() == 10, "capacity should be 10");
    TEST_ASSERT(buf.size() == 0, "size should be 0");
    TEST_ASSERT(buf.empty(), "should be empty");
    TEST_ASSERT(buf.sequence() == 0, "sequence should be 0");

    std::vector<int> dest;
    auto result = buf.copy_to(dest);
    TEST_ASSERT(result.count == 0, "copy count should be 0");
    TEST_ASSERT(result.sequence == 0, "copy sequence should be 0");
    TEST_ASSERT(dest.empty(), "dest should be empty");

    return true;
}

// Test: Basic push and copy
bool test_basic_push() {
    Ring_buffer<int> buf(10);

    buf.push(42);
    TEST_ASSERT(buf.size() == 1, "size should be 1 after push");
    TEST_ASSERT(!buf.empty(), "should not be empty");
    TEST_ASSERT(buf.sequence() == 1, "sequence should be 1");

    std::vector<int> dest;
    auto result = buf.copy_to(dest);
    TEST_ASSERT(result.count == 1, "copy count should be 1");
    TEST_ASSERT(dest.size() == 1, "dest size should be 1");
    TEST_ASSERT(dest[0] == 42, "dest[0] should be 42");

    return true;
}

// Test: Multiple pushes
bool test_multiple_pushes() {
    Ring_buffer<int> buf(10);

    for (int i = 0; i < 5; ++i) {
        buf.push(i * 10);
    }

    TEST_ASSERT(buf.size() == 5, "size should be 5");
    TEST_ASSERT(buf.sequence() == 5, "sequence should be 5");

    std::vector<int> dest;
    auto result = buf.copy_to(dest);
    TEST_ASSERT(result.count == 5, "copy count should be 5");
    TEST_ASSERT(dest.size() == 5, "dest size should be 5");

    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT(dest[i] == i * 10, "data mismatch at index");
    }

    return true;
}

// Test: Batch push
bool test_batch_push() {
    Ring_buffer<int> buf(10);

    int data[] = {1, 2, 3, 4, 5};
    buf.push_batch(data, 5);

    TEST_ASSERT(buf.size() == 5, "size should be 5");
    TEST_ASSERT(buf.sequence() == 5, "sequence should be 5");

    std::vector<int> dest;
    auto result = buf.copy_to(dest);
    TEST_ASSERT(result.count == 5, "copy count should be 5");

    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT(dest[i] == i + 1, "data mismatch at index");
    }

    return true;
}

// Test: Overwrite behavior when full
bool test_overwrite() {
    Ring_buffer<int> buf(5);

    // Fill buffer completely (0, 1, 2, 3, 4)
    for (int i = 0; i < 5; ++i) {
        buf.push(i);
    }

    TEST_ASSERT(buf.size() == 5, "size should be 5 (full)");
    TEST_ASSERT(buf.sequence() == 5, "sequence should be 5");

    // Push more, should overwrite oldest
    buf.push(100);
    buf.push(101);

    TEST_ASSERT(buf.size() == 5, "size should still be 5");
    TEST_ASSERT(buf.sequence() == 7, "sequence should be 7");

    std::vector<int> dest;
    auto result = buf.copy_to(dest);
    TEST_ASSERT(result.count == 5, "copy count should be 5");

    // Should have: 2, 3, 4, 100, 101 (oldest 0, 1 overwritten)
    TEST_ASSERT(dest[0] == 2, "dest[0] should be 2");
    TEST_ASSERT(dest[1] == 3, "dest[1] should be 3");
    TEST_ASSERT(dest[2] == 4, "dest[2] should be 4");
    TEST_ASSERT(dest[3] == 100, "dest[3] should be 100");
    TEST_ASSERT(dest[4] == 101, "dest[4] should be 101");

    return true;
}

// Test: Wraparound copy (data spans end and beginning of buffer)
bool test_wraparound_copy() {
    Ring_buffer<int> buf(5);

    // Push enough to wrap around
    for (int i = 0; i < 7; ++i) {
        buf.push(i);
    }

    // Buffer should contain: 2, 3, 4, 5, 6 (wrapped)
    std::vector<int> dest;
    auto result = buf.copy_to(dest);
    TEST_ASSERT(result.count == 5, "copy count should be 5");

    TEST_ASSERT(dest[0] == 2, "dest[0] should be 2");
    TEST_ASSERT(dest[1] == 3, "dest[1] should be 3");
    TEST_ASSERT(dest[2] == 4, "dest[2] should be 4");
    TEST_ASSERT(dest[3] == 5, "dest[3] should be 5");
    TEST_ASSERT(dest[4] == 6, "dest[4] should be 6");

    return true;
}

// Test: Clear functionality
bool test_clear() {
    Ring_buffer<int> buf(10);

    for (int i = 0; i < 5; ++i) {
        buf.push(i);
    }

    TEST_ASSERT(buf.size() == 5, "size should be 5 before clear");

    buf.clear();

    TEST_ASSERT(buf.size() == 0, "size should be 0 after clear");
    TEST_ASSERT(buf.empty(), "should be empty after clear");
    TEST_ASSERT(buf.sequence() == 0, "sequence should be 0 after clear");

    return true;
}

// Test: Thread safety (basic concurrent access)
bool test_thread_safety() {
    Ring_buffer<int> buf(1000);
    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < 10000 && !stop; ++i) {
            buf.push(i);
            if (i % 100 == 0) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer thread
    std::thread consumer([&]() {
        std::vector<int> dest;
        while (!stop) {
            auto result = buf.copy_to(dest);
            if (result.count > 0) {
                read_count.fetch_add(1);
            }
            std::this_thread::yield();
        }
    });

    // Let them run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;

    producer.join();
    consumer.join();

    // Just verify no crashes and some reads happened
    TEST_ASSERT(read_count > 0, "consumer should have read some data");
    TEST_ASSERT(buf.sequence() > 0, "producer should have pushed data");

    return true;
}

// Test: Struct data type
struct Sample {
    double timestamp;
    float value;
};

bool test_struct_data() {
    Ring_buffer<Sample> buf(10);

    buf.push({1.0, 100.0f});
    buf.push({2.0, 200.0f});
    buf.push({3.0, 300.0f});

    std::vector<Sample> dest;
    auto result = buf.copy_to(dest);

    TEST_ASSERT(result.count == 3, "copy count should be 3");
    TEST_ASSERT(dest[0].timestamp == 1.0, "dest[0].timestamp should be 1.0");
    TEST_ASSERT(dest[1].value == 200.0f, "dest[1].value should be 200.0f");
    TEST_ASSERT(dest[2].timestamp == 3.0, "dest[2].timestamp should be 3.0");

    return true;
}

int main() {
    std::cout << "Ring Buffer Test Suite\n";
    std::cout << "======================\n\n";

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_empty_buffer);
    RUN_TEST(test_basic_push);
    RUN_TEST(test_multiple_pushes);
    RUN_TEST(test_batch_push);
    RUN_TEST(test_overwrite);
    RUN_TEST(test_wraparound_copy);
    RUN_TEST(test_clear);
    RUN_TEST(test_thread_safety);
    RUN_TEST(test_struct_data);

    std::cout << "\n======================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    return failed > 0 ? 1 : 0;
}
