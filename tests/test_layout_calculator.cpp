// Tests for vnm_plot's Layout_calculator covering the formatter contract:
// the format_timestamp callback must receive both arguments as int64
// nanoseconds. The function_plotter example relies on this contract; a
// regression that started passing seconds or milliseconds would silently
// shift labels by 9 orders of magnitude.

#include "test_macros.h"

#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/layout_calculator.h>
#include <vnm_plot/core/types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace plot = vnm::plot;

namespace {

constexpr std::int64_t k_ns_per_second = 1'000'000'000LL;

struct Recorded_call
{
    std::int64_t timestamp_ns = 0;
    std::int64_t step_ns      = 0;
};

plot::Layout_calculator::parameters_t make_minimal_params(
    std::int64_t                   t_min_ns,
    std::int64_t                   t_max_ns,
    std::vector<Recorded_call>&    recorded_calls)
{
    plot::Layout_calculator::parameters_t params;
    params.v_min = 0.0f;
    params.v_max = 1.0f;
    params.t_min = t_min_ns;
    params.t_max = t_max_ns;
    params.usable_width  = 800.0;
    params.usable_height = 480.0;
    params.vbar_width    = 56.0;
    params.label_visible_height = 480.0;
    params.adjusted_font_size_in_pixels = 14.0;
    params.h_label_vertical_nudge_factor = 0.0f;
    params.measure_text_cache_key = 0;
    // Emulate a monospace font so the calculator does not need a
    // measure_text callback to estimate label widths.
    params.monospace_char_advance_px     = 8.0f;
    params.monospace_advance_is_reliable = true;
    params.measure_text_func = [](const char* text) {
        return static_cast<float>(std::strlen(text)) * 8.0f;
    };
    params.get_required_fixed_digits_func = [](double) { return 2; };
    // Recording formatter: capture every (timestamp, step) pair the
    // calculator passes through.
    params.format_timestamp_func = [&recorded_calls](
        std::int64_t timestamp_ns,
        std::int64_t step_ns) -> std::string
    {
        recorded_calls.push_back({timestamp_ns, step_ns});
        return std::string("L");
    };
    return params;
}

bool test_format_timestamp_receives_nanosecond_units()
{
    // 1-minute window centered around a modern wall-clock timestamp. The
    // calculator should produce horizontal labels and call the formatter
    // for each of them.
    constexpr std::int64_t k_t_min_ns =
        1'700'000'000LL * k_ns_per_second;     // ~late 2023
    constexpr std::int64_t k_t_max_ns =
        k_t_min_ns + 60LL * k_ns_per_second;   // 60-second span

    std::vector<Recorded_call> recorded;
    plot::Layout_calculator calc;
    auto params = make_minimal_params(k_t_min_ns, k_t_max_ns, recorded);
    auto result = calc.calculate(params);

    TEST_ASSERT(!recorded.empty(),
        "expected the layout calculator to invoke the format_timestamp "
        "callback for at least one horizontal label");
    TEST_ASSERT(!result.h_labels.empty(),
        "expected at least one horizontal label for a 60-second window");

    // Step values: build_time_steps_covering yields steps in seconds. The
    // formatter receives them as ns-quantised int64s (llround at the
    // boundary). For a 60-second span, the chosen finest step lies in the
    // 1 s ... 60 s window. Anything below 1 ms or above the span itself
    // would be a unit slip. (The calculator may also probe the formatter
    // for cache-signature purposes; both probe and label calls share the
    // same step_ns, so the step bound applies uniformly.)
    constexpr std::int64_t k_one_ms_ns = 1'000'000LL;
    for (const auto& call : recorded) {
        TEST_ASSERT(call.step_ns >= k_one_ms_ns,
            std::string("step_ns = ") + std::to_string(call.step_ns) +
            " is below 1 ms; if the formatter were receiving seconds "
            "instead of nanoseconds, every step would land here");
        TEST_ASSERT(call.step_ns <= 2 * (k_t_max_ns - k_t_min_ns),
            "step_ns is larger than the visible span; a likely sign of a "
            "ns-vs-us unit slip in the calculator -> formatter handoff");
    }

    // Timestamps: the calculator separates two kinds of formatter calls:
    //
    // 1. Format-signature probes at fixed ns-scale samples (currently 0,
    //    1.23e8, 1.23e13) used only to compute a cache-key hash. These do
    //    not depend on t_min/t_max.
    // 2. Actual label calls inside or near the visible window.
    //
    // The contract under test is that *both* receive ns-scale int64
    // values. Any actual-label call inside the visible-window region
    // confirms ns scale (a seconds-scale unit slip would compress the
    // values into a ~60 ns range starting near zero, far below the
    // expected ~1.7e18 magnitude).
    const std::int64_t margin_ns           = (k_t_max_ns - k_t_min_ns);
    bool               saw_label_in_window = false;
    for (const auto& call : recorded) {
        if (call.timestamp_ns >= k_t_min_ns - margin_ns &&
            call.timestamp_ns <= k_t_max_ns + margin_ns)
        {
            saw_label_in_window = true;
            break;
        }
    }
    TEST_ASSERT(saw_label_in_window,
        std::string("expected at least one formatter call with a timestamp "
                    "inside the visible window [") +
        std::to_string(k_t_min_ns) + " .. " + std::to_string(k_t_max_ns) +
        "]; if the calculator passed t_seconds instead of t_ns, the "
        "values would land near 1.7e9 instead of 1.7e18");

    // Sanity: the formatter must have been invoked with valid (non-negative
    // step, finite timestamp) arguments. A ns-scale arg can be any int64;
    // we only check no nonsense like INT64_MIN/MAX leaking through.
    constexpr std::int64_t k_sentinel_lo = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t k_sentinel_hi = std::numeric_limits<std::int64_t>::max();
    for (const auto& call : recorded) {
        TEST_ASSERT(call.timestamp_ns != k_sentinel_lo &&
                    call.timestamp_ns != k_sentinel_hi,
            "no formatter call should receive an INT64 sentinel timestamp");
        TEST_ASSERT(call.step_ns > 0,
            "formatter step must be positive (the calculator should never "
            "pass step_ns <= 0)");
    }

    return true;
}

bool test_format_timestamp_step_matches_nanosecond_seconds_grid()
{
    // The horizontal-axis ladder is built from build_time_steps_covering(),
    // which lives in algo.h and produces fixed multiples of seconds (with
    // sub-second 1-2-5 steps starting at 1 ms). For a few-second visible
    // window the calculator should pick a finest step from that ladder; the
    // step it passes to the formatter (after seconds -> ns conversion) must
    // round-trip exactly to one of the ladder entries.
    constexpr std::int64_t k_t_min_ns = 0LL;
    constexpr std::int64_t k_t_max_ns = 5LL * k_ns_per_second;

    std::vector<Recorded_call> recorded;
    plot::Layout_calculator calc;
    auto params = make_minimal_params(k_t_min_ns, k_t_max_ns, recorded);
    auto result = calc.calculate(params);

    TEST_ASSERT(!recorded.empty(),
        "expected the formatter to be invoked");
    (void)result;

    const auto ladder_seconds = plot::detail::build_time_steps_covering(
        static_cast<double>(k_t_max_ns - k_t_min_ns) / 1.0e9);

    // Convert ladder steps to ns and verify each recorded step matches one
    // entry exactly. If the calculator passed seconds, the ladder values
    // would be 1, 2, 5, ... rather than 1e9, 2e9, 5e9, ... and this check
    // would catch the slip.
    bool any_match = false;
    for (const auto& call : recorded) {
        bool matched = false;
        for (double step_seconds : ladder_seconds) {
            const std::int64_t step_ns_expected =
                static_cast<std::int64_t>(step_seconds * 1.0e9 + 0.5);
            if (call.step_ns == step_ns_expected) {
                matched = true;
                break;
            }
        }
        if (matched) {
            any_match = true;
        }
    }
    TEST_ASSERT(any_match,
        "expected at least one formatter step_ns to match a ladder entry "
        "after seconds -> ns conversion. Mismatch typically means the "
        "calculator passed step_ns in seconds (or in milliseconds) instead "
        "of nanoseconds.");

    return true;
}

bool test_horizontal_axis_handles_full_int64_time_span()
{
    constexpr std::int64_t k_int64_min = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t k_int64_max = std::numeric_limits<std::int64_t>::max();

    std::vector<Recorded_call> recorded;
    auto params = make_minimal_params(k_int64_min, k_int64_max, recorded);
    params.format_timestamp_revision = 1;

    plot::Layout_calculator calc;
    const auto result = calc.calculate(params);

    TEST_ASSERT(result.horizontal_seed_index >= 0,
        "full int64 timestamp range should enter horizontal-axis layout");
    TEST_ASSERT(result.horizontal_seed_step > 0.0,
        "full int64 timestamp range should compute a positive horizontal step");
    TEST_ASSERT(!recorded.empty(),
        "formatter-enabled full int64 timestamp range should call the formatter");
    TEST_ASSERT(result.h_labels.size() > 1,
        "formatter-enabled full int64 timestamp range should emit multiple horizontal labels");

    bool saw_signature_zero      = false;
    bool saw_signature_subsecond = false;
    bool saw_signature_large     = false;
    bool saw_saturated_step      = false;
    for (const auto& call : recorded) {
        saw_signature_zero = saw_signature_zero || call.timestamp_ns == 0;
        saw_signature_subsecond =
            saw_signature_subsecond || call.timestamp_ns == 123'456'789;
        saw_signature_large =
            saw_signature_large || call.timestamp_ns == 12'345'678'900'000;
        saw_saturated_step = saw_saturated_step || call.step_ns == k_int64_max;

        TEST_ASSERT(call.step_ns > 0,
            "formatter step_ns should remain positive after saturated conversion");
    }

    TEST_ASSERT(saw_signature_zero && saw_signature_subsecond && saw_signature_large,
        "formatter-enabled full int64 range should exercise format-signature probes");
    TEST_ASSERT(saw_saturated_step,
        "full int64 range should saturate formatter step_ns instead of converting out of range");

    return true;
}

bool test_vertical_labels_keep_dense_level_when_glyphs_fit()
{
    std::vector<Recorded_call> recorded;
    auto params = make_minimal_params(0LL, 60LL * k_ns_per_second, recorded);
    params.v_min = -5000.0f;
    params.v_max = 45000.0f;
    params.usable_height = 140.0;
    params.label_visible_height = params.usable_height;
    params.adjusted_font_size_in_pixels = 10.0;

    plot::Layout_calculator calc;
    const auto result = calc.calculate(params);

    const std::vector<double> expected_values{
        0.0,
        10000.0,
        20000.0,
        30000.0,
        40000.0
    };

    TEST_ASSERT(result.v_labels.size() >= expected_values.size(),
        std::string("expected dense vertical labels to fit, got ") +
            std::to_string(result.v_labels.size()));

    for (const double value : expected_values) {
        const bool found = std::any_of(
            result.v_labels.begin(),
            result.v_labels.end(),
            [value](const plot::v_label_t& label) {
                return std::abs(label.value - value) < 1e-6;
            });
        TEST_ASSERT(found,
            std::string("expected vertical label value ") + std::to_string(value));
    }

    return true;
}

} // namespace

int main()
{
    std::cout << "Layout calculator tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_format_timestamp_receives_nanosecond_units);
    RUN_TEST(test_format_timestamp_step_matches_nanosecond_seconds_grid);
    RUN_TEST(test_horizontal_axis_handles_full_int64_time_span);
    RUN_TEST(test_vertical_labels_keep_dense_level_when_glyphs_fit);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
