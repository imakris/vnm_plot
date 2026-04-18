#pragma once

// Shared test macros for the vnm_plot core test binaries. Each test file
// declares passed/failed locals at the top of main() and uses RUN_TEST to
// invoke individual `bool test_*()` functions; a failing TEST_ASSERT prints
// the offending expression and short-circuits its containing test.

#include <iostream>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(test_fn) \
    do { \
        std::cout << "Running " << #test_fn << "... "; \
        if (test_fn()) { \
            std::cout << "OK" << std::endl; \
            ++passed; \
        } \
        else { \
            std::cout << "FAIL" << std::endl; \
            ++failed; \
        } \
    } while (0)
