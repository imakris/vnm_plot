// vnm_plot interaction math tests

#include "test_macros.h"

#include <vnm_plot/core/time_units.h>
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/qt/plot_interaction_item.h>

#include <QGuiApplication>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace plot = vnm::plot;

namespace {

class test_interaction_item_t : public plot::Plot_interaction_item
{
public:
    using plot::Plot_interaction_item::mousePressEvent;
};

struct zoom_state_t
{
    double scale = 1.0;
    double velocity = 0.0;
};

bool nearly_equal(double a, double b, double epsilon = 1e-12)
{
    return std::abs(a - b) <= epsilon * std::max({1.0, std::abs(a), std::abs(b)});
}

zoom_state_t advance_zoom(double initial_velocity, const std::vector<double>& elapsed_steps)
{
    zoom_state_t state{1.0, initial_velocity};
    for (double elapsed_step : elapsed_steps) {
        state.scale *= plot::Plot_interaction_item::zoom_animation_scale_factor(state.velocity, elapsed_step);
        state.velocity = plot::Plot_interaction_item::zoom_animation_velocity_after(state.velocity, elapsed_step);
    }
    return state;
}

bool test_zoom_math_is_invariant_to_timer_cadence()
{
    const zoom_state_t single_gap = advance_zoom(4.0, {10.0});
    const zoom_state_t split_gap = advance_zoom(4.0, {4.0, 6.0});
    const zoom_state_t fine_steps = advance_zoom(
        4.0,
        {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0});

    TEST_ASSERT(nearly_equal(single_gap.scale, split_gap.scale), "scale should match for split elapsed time");
    TEST_ASSERT(nearly_equal(single_gap.velocity, split_gap.velocity), "velocity should match for split elapsed time");
    TEST_ASSERT(nearly_equal(single_gap.scale, fine_steps.scale), "scale should match for fine-grained elapsed time");
    TEST_ASSERT(nearly_equal(single_gap.velocity, fine_steps.velocity), "velocity should match for fine-grained elapsed time");

    return true;
}

bool test_zoom_math_stays_composable_across_small_velocity_decay()
{
    const zoom_state_t single_gap = advance_zoom(0.01, {10.0});
    const zoom_state_t fine_steps = advance_zoom(
        0.01,
        {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0});

    TEST_ASSERT(nearly_equal(single_gap.scale, fine_steps.scale), "small-velocity scale should stay composable");
    TEST_ASSERT(nearly_equal(single_gap.velocity, fine_steps.velocity), "small-velocity decay should stay composable");

    return true;
}

bool test_zoom_math_handles_negative_velocity()
{
    const zoom_state_t single_gap = advance_zoom(-4.0, {10.0});
    const zoom_state_t split_gap = advance_zoom(-4.0, {4.0, 6.0});
    const zoom_state_t fractional_steps = advance_zoom(
        -4.0,
        {0.3, 0.7, 1.5, 2.5, 5.0});

    TEST_ASSERT(nearly_equal(single_gap.scale, split_gap.scale), "negative scale should match for split elapsed time");
    TEST_ASSERT(nearly_equal(single_gap.velocity, split_gap.velocity), "negative velocity should match for split elapsed time");
    TEST_ASSERT(nearly_equal(single_gap.scale, fractional_steps.scale), "negative scale should match for fractional steps");
    TEST_ASSERT(nearly_equal(single_gap.velocity, fractional_steps.velocity), "negative velocity should match for fractional steps");

    return true;
}

bool test_zoom_math_handles_zero_velocity()
{
    const zoom_state_t state = advance_zoom(0.0, {0.3, 0.7, 1.5, 2.5, 5.0});

    TEST_ASSERT(nearly_equal(state.scale, 1.0), "zero velocity should keep identity scale");
    TEST_ASSERT(nearly_equal(state.velocity, 0.0), "zero velocity should stay zero");

    return true;
}

bool test_preview_thumb_press_handles_full_int64_availability()
{
    constexpr std::int64_t k_min = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t k_max = std::numeric_limits<std::int64_t>::max();

    plot::Plot_widget widget;
    widget.set_available_t_range(k_min, k_max);
    widget.set_t_range(-100, 100);
    widget.set_preview_height(20.0);

    test_interaction_item_t item;
    item.set_plot_widget(&widget);
    item.setWidth(100.0);
    item.setHeight(100.0);

    QMouseEvent press(
        QEvent::MouseButtonPress,
        QPointF{50.0, 90.0},
        QPointF{50.0, 90.0},
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    item.mousePressEvent(&press);

    TEST_ASSERT(press.isAccepted(),
        "preview-thumb press should be handled");
    TEST_ASSERT(widget.t_min() == -100 && widget.t_max() == 100,
        "center preview-thumb press should not recenter over full int64 availability");

    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    std::cout << "Plot interaction item tests" << std::endl;

    int passed = 0;
    int failed = 0;

    RUN_TEST(test_zoom_math_is_invariant_to_timer_cadence);
    RUN_TEST(test_zoom_math_stays_composable_across_small_velocity_decay);
    RUN_TEST(test_zoom_math_handles_negative_velocity);
    RUN_TEST(test_zoom_math_handles_zero_velocity);
    RUN_TEST(test_preview_thumb_press_handles_full_int64_availability);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
