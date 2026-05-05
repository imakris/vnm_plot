// vnm_plot Benchmark - Sample Types
// Packed structs for predictable GPU upload layout

#ifndef VNM_PLOT_BENCHMARK_SAMPLE_TYPES_H
#define VNM_PLOT_BENCHMARK_SAMPLE_TYPES_H

#include <cstdint>

namespace vnm::benchmark {

#pragma pack(push, 1)

/// Bar (OHLCV) sample for candlestick/line charts.
/// Packed to 28 bytes for GPU upload. Timestamp is int64 nanoseconds (the
/// vnm_plot API convention); the slot is the same 8 bytes the GPU pipeline
/// reads as fp64 seconds, with the renderer converting at upload.
struct Bar_sample {
    int64_t timestamp;   ///< 8 bytes: Nanoseconds since benchmark start
    float open;          ///< 4 bytes: Opening price
    float high;          ///< 4 bytes: Highest price
    float low;           ///< 4 bytes: Lowest price
    float close;         ///< 4 bytes: Closing price
    float volume;        ///< 4 bytes: Trading volume
};
static_assert(sizeof(Bar_sample) == 28, "Bar_sample must be 28 bytes packed");

/// Trade sample for tick-level data.
/// Packed to 16 bytes for GPU upload. Timestamp is int64 nanoseconds.
struct Trade_sample {
    int64_t timestamp;   ///< 8 bytes: Nanoseconds since benchmark start
    float price;         ///< 4 bytes: Trade price
    float size;          ///< 4 bytes: Trade size/quantity
};
static_assert(sizeof(Trade_sample) == 16, "Trade_sample must be 16 bytes packed");

#pragma pack(pop)

}  // namespace vnm::benchmark

#endif  // VNM_PLOT_BENCHMARK_SAMPLE_TYPES_H
