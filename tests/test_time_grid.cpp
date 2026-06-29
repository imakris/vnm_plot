#include "test_macros.h"

#include <vnm_plot/core/time_grid.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace plot = vnm::plot;

namespace {

bool nearly_equal(double actual, double expected, double tolerance = 1e-5)
{
    return std::abs(actual - expected) <= tolerance;
}

float expected_alpha(float spacing_px)
{
    constexpr double k_cell_span_min = 1.0;
    constexpr double k_fade_den      = 59.0;
    constexpr double k_alpha_base    = 0.75;
    const double fade = (double(spacing_px) - k_cell_span_min) / k_fade_den;
    return static_cast<float>(std::clamp(fade, 0.0, 1.0) * k_alpha_base);
}

float expected_thickness(float alpha)
{
    constexpr float k_alpha_base = 0.75f;
    return 0.6f + 0.6f * (alpha / k_alpha_base);
}

std::string describe_levels(const plot::grid_layer_params_t& levels)
{
    std::ostringstream out;
    out << "count=" << levels.count;
    for (int i = 0; i < levels.count; ++i) {
        out << " [" << i << "] spacing=" << levels.spacing_px[i]
            << " start=" << levels.start_px[i];
    }
    return out.str();
}

bool test_time_grid_layers_preserve_spacing_and_style()
{
    const plot::grid_layer_params_t levels =
        plot::build_time_grid_layers(0.0, 60.0, 600.0, 10.0);

    const std::vector<float> expected_spacings = {600.0f, 300.0f, 100.0f, 20.0f, 10.0f, 5.0f};

    TEST_ASSERT(levels.count == static_cast<int>(expected_spacings.size()),
        std::string("60-second window levels mismatch: ") + describe_levels(levels));

    for (int i = 0; i < levels.count; ++i) {
        const float alpha = expected_alpha(expected_spacings[static_cast<std::size_t>(i)]);
        TEST_ASSERT(nearly_equal(levels.spacing_px[i], expected_spacings[static_cast<std::size_t>(i)]),
            "grid level spacing should match the preserved time-step ladder");
        TEST_ASSERT(nearly_equal(levels.start_px[i], 0.0f),
            "zero-origin grid levels should start at zero pixels");
        TEST_ASSERT(nearly_equal(levels.alpha[i], alpha),
            "grid level alpha should follow the preserved fade ramp");
        TEST_ASSERT(nearly_equal(levels.thickness_px[i], expected_thickness(alpha)),
            "grid level thickness should follow the preserved alpha ramp");
    }

    return true;
}

bool test_time_grid_layers_preserve_phase()
{
    const plot::grid_layer_params_t levels =
        plot::build_time_grid_layers(7.25, 67.25, 600.0, 10.0);

    const std::vector<float> expected_starts = {527.5f, 227.5f, 27.5f, 7.5f, 7.5f, 2.5f};

    TEST_ASSERT(levels.count == static_cast<int>(expected_starts.size()),
        std::string("shifted 60-second window levels mismatch: ") + describe_levels(levels));

    for (int i = 0; i < levels.count; ++i) {
        TEST_ASSERT(nearly_equal(levels.start_px[i], expected_starts[static_cast<std::size_t>(i)]),
            "grid level start should preserve get_shift phase alignment");
    }

    return true;
}

bool test_time_grid_layers_reject_degenerate_ranges()
{
    bool dropped_non_multiple_step = true;
    TEST_ASSERT(plot::build_time_grid_layers(
            5.0,
            5.0,
            600.0,
            10.0,
            &dropped_non_multiple_step).count == 0,
        "zero-width time range should produce no grid levels");
    TEST_ASSERT(!dropped_non_multiple_step,
        "degenerate ranges should clear the non-multiple diagnostic flag");
    TEST_ASSERT(plot::build_time_grid_layers(6.0, 5.0, 600.0, 10.0).count == 0,
        "negative time range should produce no grid levels");
    TEST_ASSERT(plot::build_time_grid_layers(0.0, 60.0, 0.0, 10.0).count == 0,
        "zero pixel width should produce no grid levels");
    TEST_ASSERT(plot::build_time_grid_layers(0.0, 60.0, -1.0, 10.0).count == 0,
        "negative pixel width should produce no grid levels");

    return true;
}

bool test_time_grid_layers_do_not_report_non_multiple_for_current_ladder()
{
    bool dropped_non_multiple_step = true;
    const plot::grid_layer_params_t levels = plot::build_time_grid_layers(
        0.0,
        60.0,
        600.0,
        10.0,
        &dropped_non_multiple_step);

    TEST_ASSERT(levels.count > 0, "representative grid should produce levels");
    TEST_ASSERT(!dropped_non_multiple_step,
        "current time-step ladder should not report non-multiple diagnostics");

    return true;
}

} // namespace

int main()
{
    int passed = 0;
    int failed = 0;

    RUN_TEST(test_time_grid_layers_preserve_spacing_and_style);
    RUN_TEST(test_time_grid_layers_preserve_phase);
    RUN_TEST(test_time_grid_layers_reject_degenerate_ranges);
    RUN_TEST(test_time_grid_layers_do_not_report_non_multiple_for_current_ladder);

    std::cout << "\nTime grid tests: " << passed << " passed, " << failed << " failed\n";

    return failed > 0 ? 1 : 0;
}
