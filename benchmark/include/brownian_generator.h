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
        : m_config(config)
        , m_rng(config.seed)
        , m_normal(0.0, 1.0)
        , m_current_price(config.initial_price)
        , m_current_time(0.0)
    {
        // Pre-compute constants for GBM formula
        // drift_term = (mu - sigma^2/2) * dt
        m_drift_term = (m_config.drift - 0.5 * m_config.volatility * m_config.volatility) * m_config.time_step;
        // vol_term = sigma * sqrt(dt)
        m_vol_term = m_config.volatility * std::sqrt(m_config.time_step);
    }

    /// Generate the next bar sample.
    /// Simulates one time step of price movement and generates OHLC data.
    Bar_sample next_bar() {
        Bar_sample bar;
        bar.timestamp = m_current_time;
        bar.open = static_cast<float>(m_current_price);

        // Simulate intra-bar volatility for OHLC
        double high = m_current_price;
        double low = m_current_price;

        // Generate a few sub-steps within the bar for realistic OHLC
        constexpr int sub_steps = 4;
        for (int i = 0; i < sub_steps; ++i) {
            double z = m_normal(m_rng);
            double sub_drift = m_drift_term / sub_steps;
            double sub_vol = m_vol_term / std::sqrt(static_cast<double>(sub_steps));
            m_current_price *= std::exp(sub_drift + sub_vol * z);

            if (m_current_price > high) high = m_current_price;
            if (m_current_price < low) low = m_current_price;
        }

        bar.high = static_cast<float>(high);
        bar.low = static_cast<float>(low);
        bar.close = static_cast<float>(m_current_price);

        // Generate random volume (log-normal distribution)
        double volume_z = m_normal(m_rng);
        bar.volume = static_cast<float>(std::exp(10.0 + volume_z));  // Typical volume scale

        m_current_time += m_config.time_step;
        return bar;
    }

    /// Generate the next trade sample.
    Trade_sample next_trade() {
        Trade_sample trade;
        trade.timestamp = m_current_time;

        // Single price step
        double z = m_normal(m_rng);
        m_current_price *= std::exp(m_drift_term + m_vol_term * z);
        trade.price = static_cast<float>(m_current_price);

        // Generate random trade size
        double size_z = m_normal(m_rng);
        trade.size = static_cast<float>(std::max(1.0, std::exp(3.0 + size_z)));

        m_current_time += m_config.time_step;
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
        m_rng.seed(m_config.seed);
        m_normal.reset();  // Clear any cached values
        m_current_price = m_config.initial_price;
        m_current_time = 0.0;
    }

    /// Get current price.
    double current_price() const { return m_current_price; }

    /// Get current time.
    double current_time() const { return m_current_time; }

    /// Get configuration.
    const Config& config() const { return m_config; }

private:
    Config m_config;
    std::mt19937_64 m_rng;
    std::normal_distribution<double> m_normal;
    double m_current_price;
    double m_current_time;

    // Pre-computed constants
    double m_drift_term;  ///< (mu - sigma^2/2) * dt
    double m_vol_term;    ///< sigma * sqrt(dt)
};

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_BROWNIAN_GENERATOR_H
