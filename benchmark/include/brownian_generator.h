// vnm_plot Benchmark - Brownian Motion Generator
// Generates synthetic price data using geometric Brownian motion

#ifndef VNM_PLOT_BENCHMARK_BROWNIAN_GENERATOR_H
#define VNM_PLOT_BENCHMARK_BROWNIAN_GENERATOR_H

#include "sample_types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>

namespace vnm::benchmark {

/// Generates synthetic price data using geometric Brownian motion.
///
/// The geometric Brownian motion formula:
/// S(t+dt) = S(t) * exp((mu - sigma^2/2) * dt + sigma * sqrt(dt) * Z)
/// where Z ~ N(0,1)
class Brownian_generator {
public:
    /// Configuration for the generator.
    struct Config {
        double initial_price = 100.0;   ///< Starting price
        double drift = 0.0001;          ///< mu: expected return per step
        double volatility = 0.02;       ///< sigma: standard deviation per step
        double time_step = 0.001;       ///< Seconds between samples
        uint64_t seed = 12345;          ///< RNG seed for reproducibility
    };

    /// Construct a generator with the given configuration.
    explicit Brownian_generator(const Config& config)
        : config_(config)
        , rng_(config.seed)
        , normal_(0.0, 1.0)
        , current_price_(config.initial_price)
        , current_time_(0.0)
    {
        // Pre-compute constants for GBM formula
        // drift_term = (mu - sigma^2/2) * dt
        drift_term_ = (config_.drift - 0.5 * config_.volatility * config_.volatility) * config_.time_step;
        // vol_term = sigma * sqrt(dt)
        vol_term_ = config_.volatility * std::sqrt(config_.time_step);
    }

    /// Generate the next bar sample.
    /// Simulates one time step of price movement and generates OHLC data.
    Bar_sample next_bar() {
        Bar_sample bar;
        bar.timestamp = current_time_;
        bar.open = static_cast<float>(current_price_);

        // Simulate intra-bar volatility for OHLC
        double high = current_price_;
        double low = current_price_;

        // Generate a few sub-steps within the bar for realistic OHLC
        constexpr int sub_steps = 4;
        for (int i = 0; i < sub_steps; ++i) {
            double z = normal_(rng_);
            double sub_drift = drift_term_ / sub_steps;
            double sub_vol = vol_term_ / std::sqrt(static_cast<double>(sub_steps));
            current_price_ *= std::exp(sub_drift + sub_vol * z);

            if (current_price_ > high) high = current_price_;
            if (current_price_ < low) low = current_price_;
        }

        bar.high = static_cast<float>(high);
        bar.low = static_cast<float>(low);
        bar.close = static_cast<float>(current_price_);

        // Generate random volume (log-normal distribution)
        double volume_z = normal_(rng_);
        bar.volume = static_cast<float>(std::exp(10.0 + volume_z));  // Typical volume scale

        current_time_ += config_.time_step;
        return bar;
    }

    /// Generate the next trade sample.
    Trade_sample next_trade() {
        Trade_sample trade;
        trade.timestamp = current_time_;

        // Single price step
        double z = normal_(rng_);
        current_price_ *= std::exp(drift_term_ + vol_term_ * z);
        trade.price = static_cast<float>(current_price_);

        // Generate random trade size
        double size_z = normal_(rng_);
        trade.size = static_cast<float>(std::max(1.0, std::exp(3.0 + size_z)));

        current_time_ += config_.time_step;
        return trade;
    }

    /// Generate multiple bar samples.
    /// @param out Output buffer (must have space for count samples)
    /// @param count Number of samples to generate
    void generate_bars(Bar_sample* out, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = next_bar();
        }
    }

    /// Generate multiple trade samples.
    /// @param out Output buffer (must have space for count samples)
    /// @param count Number of samples to generate
    void generate_trades(Trade_sample* out, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = next_trade();
        }
    }

    /// Reset the generator to initial state.
    void reset() {
        rng_.seed(config_.seed);
        normal_.reset();  // Clear any cached values
        current_price_ = config_.initial_price;
        current_time_ = 0.0;
    }

    /// Get current price.
    double current_price() const { return current_price_; }

    /// Get current time.
    double current_time() const { return current_time_; }

    /// Get configuration.
    const Config& config() const { return config_; }

private:
    Config config_;
    std::mt19937_64 rng_;
    std::normal_distribution<double> normal_;
    double current_price_;
    double current_time_;

    // Pre-computed constants
    double drift_term_;  ///< (mu - sigma^2/2) * dt
    double vol_term_;    ///< sigma * sqrt(dt)
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_BROWNIAN_GENERATOR_H
