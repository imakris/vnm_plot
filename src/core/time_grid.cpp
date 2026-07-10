#include <vnm_plot/core/time_grid.h>
#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/constants.h>

#include <algorithm>
#include <cmath>

namespace vnm::plot {
namespace {

bool is_integer_time_grid_multiple(double parent, double child)
{
    if (!(parent > 0.0) || !(child > 0.0)) {
        return false;
    }

    const double ratio   = parent / child;
    const double rounded = std::round(ratio);
    if (rounded < 1.0) {
        return false;
    }

    const double tol = std::min(1e-3, std::max(1e-8, std::abs(ratio) * 1e-12));
    return std::abs(ratio - rounded) <= tol;
}

float compute_grid_alpha(float spacing_px, double cell_span_min, double fade_den)
{
    const double fade = (double(spacing_px) - cell_span_min) / fade_den;
    return static_cast<float>(std::clamp(fade, 0.0, 1.0) * detail::k_grid_line_alpha_base);
}

void append_grid_level(
    grid_layer_params_t&   levels,
    float                  spacing_px,
    float                  start_px,
    double                 cell_span_min,
    double                 fade_den)
{
    levels.spacing_px[levels.count] = spacing_px;
    levels.start_px[levels.count]   = start_px;
    const float alpha = compute_grid_alpha(spacing_px, cell_span_min, fade_den);
    levels.alpha[levels.count]        = alpha;
    levels.thickness_px[levels.count] = 0.6f + 0.6f * (alpha / detail::k_grid_line_alpha_base);
    ++levels.count;
}

} // anonymous namespace

grid_layer_params_t build_time_grid_layers(
    double t_min_seconds,
    double t_max_seconds,
    double width_px,
    double font_px,
    bool*  out_dropped_non_multiple_step_or_null)
{
    if (out_dropped_non_multiple_step_or_null) {
        *out_dropped_non_multiple_step_or_null = false;
    }

    grid_layer_params_t levels;
    const double range = t_max_seconds - t_min_seconds;
    if (!(range > 0.0) || !(width_px > 0.0)) {
        return levels;
    }

    const double cell_span_min = font_px * detail::k_cell_span_min_factor;
    const double fade_den = std::max<double>(
        1e-6,
        font_px * (detail::k_cell_span_max_factor - detail::k_cell_span_min_factor));

    const double px_per_unit = width_px / range;
    const auto   steps       = detail::build_time_steps_covering(range);
    int          idx         = std::max(0, detail::find_time_step_start_index(steps, range));
    while (idx + 1               < static_cast<int>(steps.size()) &&
        steps[idx] * px_per_unit < cell_span_min)
    {
        ++idx;
    }

    double last_step = 0.0;
    for (; idx >= 0 && levels.count < grid_layer_params_t::k_max_levels; --idx) {
        const double step       = steps[idx];
        const float  spacing_px = static_cast<float>(step * px_per_unit);
        if (spacing_px < cell_span_min) {
            break;
        }
        if (last_step > 0.0 && !is_integer_time_grid_multiple(last_step, step)) {
            if (out_dropped_non_multiple_step_or_null) {
                *out_dropped_non_multiple_step_or_null = true;
            }
            continue;
        }

        const double shift_units = detail::get_shift(step, t_min_seconds);
        append_grid_level(
            levels,
            spacing_px,
            static_cast<float>(shift_units * px_per_unit),
            cell_span_min,
            fade_den);
        last_step = step;
    }

    return levels;
}

} // namespace vnm::plot
