// vnm_plot Benchmark - Brownian Generator Tests

#include "brownian_generator.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace vnm::benchmark;

// Tolerance for floating-point comparisons (handles cross-platform differences)
constexpr float FLOAT_TOLERANCE = 1e-6f;
constexpr double DOUBLE_TOLERANCE = 1e-12;

bool float_eq(float a, float b, float tol = FLOAT_TOLERANCE) {
    return std::abs(a - b) <= tol;
}

bool double_eq(double a, double b, double tol = DOUBLE_TOLERANCE) {
    return std::abs(a - b) <= tol;
}

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

// Test: Struct sizes are correct (packed)
bool test_struct_sizes() {
    // Use static_assert for compile-time size checks (avoids C4127 warning)
    static_assert(sizeof(Bar_sample) == 28, "Bar_sample should be 28 bytes");
    static_assert(sizeof(Trade_sample) == 16, "Trade_sample should be 16 bytes");
    return true;
}

// Test: Default configuration
bool test_default_config() {
    Brownian_generator::Config config;
    Brownian_generator gen(config);

    TEST_ASSERT(gen.current_price() == 100.0, "initial price should be 100.0");
    TEST_ASSERT(gen.current_time() == 0.0, "initial time should be 0.0");

    return true;
}

// Test: Bar generation produces valid OHLC
bool test_bar_generation() {
    Brownian_generator::Config config;
    config.seed = 42;
    Brownian_generator gen(config);

    Bar_sample bar = gen.next_bar();

    TEST_ASSERT(bar.timestamp == 0.0, "first bar timestamp should be 0.0");
    TEST_ASSERT(bar.open > 0.0f, "open should be positive");
    TEST_ASSERT(bar.high >= bar.open, "high should be >= open");
    TEST_ASSERT(bar.high >= bar.close, "high should be >= close");
    TEST_ASSERT(bar.low <= bar.open, "low should be <= open");
    TEST_ASSERT(bar.low <= bar.close, "low should be <= close");
    TEST_ASSERT(bar.low <= bar.high, "low should be <= high");
    TEST_ASSERT(bar.volume > 0.0f, "volume should be positive");

    return true;
}

// Test: Trade generation produces valid data
bool test_trade_generation() {
    Brownian_generator::Config config;
    config.seed = 42;
    Brownian_generator gen(config);

    Trade_sample trade = gen.next_trade();

    TEST_ASSERT(trade.timestamp == 0.0, "first trade timestamp should be 0.0");
    TEST_ASSERT(trade.price > 0.0f, "price should be positive");
    TEST_ASSERT(trade.size > 0.0f, "size should be positive");

    return true;
}

// Test: Time advances correctly
bool test_time_advancement() {
    Brownian_generator::Config config;
    config.time_step = 0.01;
    Brownian_generator gen(config);

    gen.next_bar();
    TEST_ASSERT(std::abs(gen.current_time() - 0.01) < 1e-9, "time should advance by time_step");

    gen.next_bar();
    TEST_ASSERT(std::abs(gen.current_time() - 0.02) < 1e-9, "time should advance by time_step");

    return true;
}

// Test: Batch generation
bool test_batch_generation() {
    Brownian_generator::Config config;
    config.seed = 42;
    Brownian_generator gen(config);

    std::vector<Bar_sample> bars(100);
    gen.generate_bars(bars.data(), bars.size());

    TEST_ASSERT(bars[0].timestamp == 0.0, "first bar timestamp should be 0.0");
    TEST_ASSERT(bars[99].timestamp > bars[0].timestamp, "timestamps should increase");

    // Check all bars are valid
    for (const auto& bar : bars) {
        TEST_ASSERT(bar.low <= bar.high, "low should be <= high");
        TEST_ASSERT(bar.close >= bar.low && bar.close <= bar.high, "close in range");
    }

    return true;
}

// Test: Reproducibility with same seed
bool test_reproducibility() {
    Brownian_generator::Config config;
    config.seed = 12345;

    Brownian_generator gen1(config);
    Brownian_generator gen2(config);

    for (int i = 0; i < 10; ++i) {
        Bar_sample bar1 = gen1.next_bar();
        Bar_sample bar2 = gen2.next_bar();

        TEST_ASSERT(float_eq(bar1.close, bar2.close), "same seed should give same results");
        TEST_ASSERT(float_eq(bar1.volume, bar2.volume), "same seed should give same volume");
    }

    return true;
}

// Test: Reset functionality
bool test_reset() {
    Brownian_generator::Config config;
    config.seed = 42;
    Brownian_generator gen(config);

    Bar_sample bar1 = gen.next_bar();
    gen.next_bar();  // Advance
    gen.next_bar();

    gen.reset();

    Bar_sample bar_after_reset = gen.next_bar();

    TEST_ASSERT(float_eq(bar1.close, bar_after_reset.close), "reset should reproduce same sequence");
    TEST_ASSERT(double_eq(gen.current_time(), config.time_step), "time should be reset");

    return true;
}

// Test: Different seeds produce different sequences
bool test_different_seeds() {
    Brownian_generator::Config config1;
    config1.seed = 1;

    Brownian_generator::Config config2;
    config2.seed = 2;

    Brownian_generator gen1(config1);
    Brownian_generator gen2(config2);

    Bar_sample bar1 = gen1.next_bar();
    Bar_sample bar2 = gen2.next_bar();

    TEST_ASSERT(bar1.close != bar2.close, "different seeds should produce different results");

    return true;
}

// Test: Volatility affects price variance
bool test_volatility_effect() {
    Brownian_generator::Config low_vol_config;
    low_vol_config.volatility = 0.001;
    low_vol_config.seed = 42;

    Brownian_generator::Config high_vol_config;
    high_vol_config.volatility = 0.1;
    high_vol_config.seed = 42;

    Brownian_generator low_vol_gen(low_vol_config);
    Brownian_generator high_vol_gen(high_vol_config);

    // Generate 1000 samples and compute variance
    double low_vol_sum_sq = 0.0;
    double high_vol_sum_sq = 0.0;
    double prev_low = low_vol_gen.current_price();
    double prev_high = high_vol_gen.current_price();

    for (int i = 0; i < 1000; ++i) {
        Bar_sample low_bar = low_vol_gen.next_bar();
        Bar_sample high_bar = high_vol_gen.next_bar();

        double low_return = std::log(low_bar.close / prev_low);
        double high_return = std::log(high_bar.close / prev_high);

        low_vol_sum_sq += low_return * low_return;
        high_vol_sum_sq += high_return * high_return;

        prev_low = low_bar.close;
        prev_high = high_bar.close;
    }

    // High volatility should have higher variance
    TEST_ASSERT(high_vol_sum_sq > low_vol_sum_sq, "higher volatility should produce larger variance");

    return true;
}

int main() {
    std::cout << "Brownian Generator Test Suite\n";
    std::cout << "=============================\n\n";

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_struct_sizes);
    RUN_TEST(test_default_config);
    RUN_TEST(test_bar_generation);
    RUN_TEST(test_trade_generation);
    RUN_TEST(test_time_advancement);
    RUN_TEST(test_batch_generation);
    RUN_TEST(test_reproducibility);
    RUN_TEST(test_reset);
    RUN_TEST(test_different_seeds);
    RUN_TEST(test_volatility_effect);

    std::cout << "\n=============================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    return failed > 0 ? 1 : 0;
}
