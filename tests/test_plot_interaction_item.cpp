// vnm_plot interaction math tests

#include "test_macros.h"

#include <vnm_plot/core/access_policy.h>
#include <vnm_plot/core/time_units.h>
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/qt/plot_interaction_item.h>
#include <vnm_plot/qt/plot_time_axis.h>

#include <QGuiApplication>
#include <QMouseEvent>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
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
    double         scale    = 1.0;
    double         velocity = 0.0;
};

struct indicator_sample_t
{
    std::int64_t   t_ns  = 0;
    float          value = 0.0f;
};

bool nearly_equal(double a, double b, double epsilon = 1e-12)
{
    return std::abs(a - b) <= epsilon * std::max({1.0, std::abs(a), std::abs(b)});
}

zoom_state_t advance_zoom(double initial_velocity, const std::vector<double>& elapsed_steps)
{
    zoom_state_t state{1.0, initial_velocity};
    for (double elapsed_step : elapsed_steps) {
        state.scale    *= plot::Plot_interaction_item::zoom_animation_scale_factor(state.velocity, elapsed_step);
        state.velocity  = plot::Plot_interaction_item::zoom_animation_velocity_after(state.velocity, elapsed_step);
    }
    return state;
}

std::shared_ptr<plot::series_data_t> make_sample_series(
    std::vector<indicator_sample_t>    samples,
    plot::Series_interpolation         interpolation,
    plot::Empty_window_behavior        empty_window_behavior = plot::Empty_window_behavior::DRAW_NOTHING)
{
    auto source = std::make_shared<plot::Vector_data_source<indicator_sample_t>>(
        std::move(samples));

    plot::Data_access_policy_typed<indicator_sample_t> access;
    access.get_timestamp = [](const indicator_sample_t& sample) {
        return sample.t_ns;
    };
    access.get_value = [](const indicator_sample_t& sample) {
        return sample.value;
    };

    auto series = std::make_shared<plot::series_data_t>();
    series->series_label          = "indicator";
    series->interpolation         = interpolation;
    series->empty_window_behavior = empty_window_behavior;
    series->data_source           = source;
    series->access                = access.erase();

    return series;
}

void configure_view(
    plot::Plot_widget&         widget,
    std::int64_t               tmin_ns,
    std::int64_t               tmax_ns,
    float                      vmin,
    float                      vmax)
{
    plot::Plot_view view;
    view.t_range           = std::pair<qint64, qint64>{tmin_ns, tmax_ns};
    view.t_available_range = std::pair<qint64, qint64>{tmin_ns, tmax_ns};
    view.v_range           = std::pair<float, float>{vmin, vmax};
    view.v_auto            = false;

    widget.set_view(view);
}

void configure_time_window(
    plot::Plot_widget&         widget,
    std::int64_t               tmin_ns,
    std::int64_t               tmax_ns,
    std::int64_t               t_available_min_ns,
    std::int64_t               t_available_max_ns)
{
    widget.set_t_range(tmin_ns, tmax_ns);
    widget.set_available_t_range(t_available_min_ns, t_available_max_ns);
}

void configure_indicator_widget(
    plot::Plot_widget&         widget,
    plot::Series_interpolation interpolation)
{
    constexpr std::int64_t k_second_ns = 1'000'000'000LL;

    configure_view(widget, 0, k_second_ns, 0.0f, 10.0f);
    widget.add_series(1, make_sample_series(
        {
            { 0, 0.0f },
            {k_second_ns, 10.0f}
        },
        interpolation));
}

bool test_zoom_math_is_invariant_to_timer_cadence()
{
    const zoom_state_t single_gap = advance_zoom(4.0, {10.0});
    const zoom_state_t split_gap  = advance_zoom(4.0, {4.0, 6.0});
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
    const zoom_state_t split_gap  = advance_zoom(-4.0, {4.0, 6.0});
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

bool test_indicator_samples_linearly_interpolate_between_samples()
{
    plot::Plot_widget widget;
    configure_indicator_widget(widget, plot::Series_interpolation::LINEAR);

    const QVariantList samples = widget.get_indicator_samples(500.0, 100.0, 100.0);
    TEST_ASSERT(samples.size() == 1, "linear indicator query should return one sample");

    const QVariantMap entry = samples.front().toMap();
    TEST_ASSERT(nearly_equal(entry.value("y").toDouble(), 5.0),
        "linear indicator query should interpolate y between neighboring samples");
    TEST_ASSERT(nearly_equal(entry.value("x").toDouble(), 500.0),
        "linear indicator query should report the requested timestamp in milliseconds");

    return true;
}

bool test_indicator_samples_step_after_holds_previous_sample()
{
    plot::Plot_widget widget;
    configure_indicator_widget(widget, plot::Series_interpolation::STEP_AFTER);

    const QVariantList samples = widget.get_indicator_samples(500.0, 100.0, 100.0);
    TEST_ASSERT(samples.size() == 1, "step-after indicator query should return one sample");

    const QVariantMap entry = samples.front().toMap();
    TEST_ASSERT(nearly_equal(entry.value("y").toDouble(), 0.0),
        "step-after indicator query should hold the previous sample value");
    TEST_ASSERT(nearly_equal(entry.value("x").toDouble(), 500.0),
        "step-after indicator query should keep the requested timestamp in milliseconds");

    return true;
}

bool test_nearest_samples_choose_closer_sample()
{
    plot::Plot_widget widget;
    configure_indicator_widget(widget, plot::Series_interpolation::LINEAR);

    const QVariantList samples = widget.get_nearest_samples(750.0, 100.0, 100.0);
    TEST_ASSERT(samples.size() == 1, "nearest indicator query should return one sample");

    const QVariantMap entry = samples.front().toMap();
    TEST_ASSERT(nearly_equal(entry.value("y").toDouble(), 10.0),
        "nearest query should choose the closer second sample");
    TEST_ASSERT(nearly_equal(entry.value("x").toDouble(), 1000.0),
        "nearest query should report the resolved sample timestamp in milliseconds");

    return true;
}

bool test_auto_adjust_view_uses_visible_samples_for_value_and_time_range()
{
    plot::Plot_widget widget;
    configure_view(widget, 5, 25, -100.0f, 100.0f);
    widget.add_series(1, make_sample_series(
        {
            { 0,  100.0f },
            { 10, 2.0f   },
            { 20, 8.0f   },
            { 30, -100.0f}
        },
        plot::Series_interpolation::LINEAR));

    widget.auto_adjust_view(true, 0.0, false);

    TEST_ASSERT(nearly_equal(widget.v_min(), 2.0),
        "auto-adjust should use the visible value minimum");
    TEST_ASSERT(nearly_equal(widget.v_max(), 8.0),
        "auto-adjust should use the visible value maximum");
    TEST_ASSERT(widget.t_min() == 10 && widget.t_max() == 20,
        "auto-adjust should shrink time range to visible samples");

    return true;
}

bool test_auto_adjust_view_includes_step_after_held_sample()
{
    plot::Plot_widget widget;
    configure_view(widget, 10, 20, -100.0f, 100.0f);
    widget.add_series(1, make_sample_series(
        {
            { 0, 2.0f },
            {15, 8.0f}
        },
        plot::Series_interpolation::STEP_AFTER));

    widget.auto_adjust_view(false, 0.0, false);

    TEST_ASSERT(nearly_equal(widget.v_min(), 2.0),
        "step-after auto-adjust should include the held value before the window");
    TEST_ASSERT(nearly_equal(widget.v_max(), 8.0),
        "step-after auto-adjust should include the in-window value");

    return true;
}

bool test_shared_vbar_explicit_width_publishes_when_sync_enabled()
{
    plot::Plot_time_axis shared_axis;
    plot::Plot_widget widget;

    shared_axis.set_sync_vbar_width(true);
    widget.set_time_axis(&shared_axis);
    widget.set_vbar_width(64.0);

    TEST_ASSERT(nearly_equal(shared_axis.shared_vbar_width_px(), widget.vbar_width_pixels()),
        "explicit vbar width should publish to the shared axis when sync is enabled");

    return true;
}

bool test_shared_vbar_attach_publishes_existing_current_width()
{
    plot::Plot_time_axis shared_axis;
    plot::Plot_widget widget;

    widget.set_vbar_width(80.0);
    const double owner_width_px = widget.vbar_width_pixels();

    shared_axis.set_sync_vbar_width(true);
    widget.set_time_axis(&shared_axis);

    TEST_ASSERT(nearly_equal(shared_axis.shared_vbar_width_px(), owner_width_px),
        "attaching to a sync-enabled axis should publish the widget's current vbar width");

    return true;
}

bool test_shared_vbar_enabling_sync_publishes_current_owner_width()
{
    plot::Plot_time_axis shared_axis;
    plot::Plot_widget widget;

    widget.set_time_axis(&shared_axis);
    widget.set_vbar_width(96.0);

    TEST_ASSERT(nearly_equal(shared_axis.shared_vbar_width_px(), 0.0),
        "disabled vbar sync should not publish the current owner width");

    const double owner_width_px = widget.vbar_width_pixels();
    shared_axis.set_sync_vbar_width(true);

    TEST_ASSERT(nearly_equal(shared_axis.shared_vbar_width_px(), owner_width_px),
        "enabling vbar sync should publish the current owner width");

    return true;
}

bool test_widget_local_available_clamp_matches_shared_axis()
{
    plot::Plot_widget local_widget;
    configure_time_window(local_widget, 100, 200, 0, 500);

    plot::Plot_time_axis shared_axis;
    plot::Plot_widget shared_widget;
    shared_widget.set_time_axis(&shared_axis);
    configure_time_window(shared_widget, 100, 200, 0, 500);

    local_widget.set_available_t_range(0, 150);
    shared_widget.set_available_t_range(0, 150);

    TEST_ASSERT(local_widget.t_min() == shared_widget.t_min(),
        "widget-local available clamp should match shared-axis t_min");
    TEST_ASSERT(local_widget.t_max() == shared_widget.t_max(),
        "widget-local available clamp should match shared-axis t_max");
    TEST_ASSERT(local_widget.t_min() == 50 && local_widget.t_max() == 150,
        "available clamp should preserve the 100 ns view span at the right edge");

    return true;
}

bool test_widget_local_preview_adjustment_matches_shared_axis()
{
    constexpr std::int64_t k_min = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t k_max = std::numeric_limits<std::int64_t>::max();

    plot::Plot_widget local_widget;
    configure_time_window(local_widget, k_min, k_min + 100, k_min, k_max);

    plot::Plot_time_axis shared_axis;
    plot::Plot_widget shared_widget;
    shared_widget.set_time_axis(&shared_axis);
    configure_time_window(shared_widget, k_min, k_min + 100, k_min, k_max);

    local_widget.adjust_t_from_mouse_diff_on_preview(100.0, 75.0);
    shared_widget.adjust_t_from_mouse_diff_on_preview(100.0, 75.0);

    TEST_ASSERT(local_widget.t_min() == shared_widget.t_min(),
        "widget-local preview drag should match shared-axis t_min");
    TEST_ASSERT(local_widget.t_max() == shared_widget.t_max(),
        "widget-local preview drag should match shared-axis t_max");

    const auto moved_span = plot::positive_span_ns(
        local_widget.t_min(),
        local_widget.t_max());
    TEST_ASSERT(moved_span && *moved_span == 100,
        "widget-local preview drag should preserve the 100 ns view span");

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
    RUN_TEST(test_indicator_samples_linearly_interpolate_between_samples);
    RUN_TEST(test_indicator_samples_step_after_holds_previous_sample);
    RUN_TEST(test_nearest_samples_choose_closer_sample);
    RUN_TEST(test_auto_adjust_view_uses_visible_samples_for_value_and_time_range);
    RUN_TEST(test_auto_adjust_view_includes_step_after_held_sample);
    RUN_TEST(test_shared_vbar_explicit_width_publishes_when_sync_enabled);
    RUN_TEST(test_shared_vbar_attach_publishes_existing_current_width);
    RUN_TEST(test_shared_vbar_enabling_sync_publishes_current_owner_width);
    RUN_TEST(test_widget_local_available_clamp_matches_shared_axis);
    RUN_TEST(test_widget_local_preview_adjustment_matches_shared_axis);
    RUN_TEST(test_preview_thumb_press_handles_full_int64_availability);

    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
