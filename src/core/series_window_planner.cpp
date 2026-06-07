#include "series_window_planner.h"

#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/plot_config.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace vnm::plot::detail {

namespace {

void reset_snapshot_cache(Series_window_snapshot_cache& cache)
{
    cache = Series_window_snapshot_cache{};
}

} // anonymous namespace

Series_view_plan plan_series_window(const series_window_plan_request_t& request)
{
    Series_view_plan plan;
    plan.series_id = request.series_id;
    plan.view_kind = request.view_kind;
    plan.source = request.data_source;
    plan.access = request.access;
    plan.lod_scale = 1;
    plan.interpolation = request.interpolation;
    plan.empty_window_behavior = request.empty_window_behavior;
    plan.style = request.style;

    if (!request.planner_state ||
        !request.snapshot_cache ||
        !request.data_source ||
        !request.access ||
        !request.scales)
    {
        return plan;
    }

    auto& state = *request.planner_state;
    auto& snapshot_cache = *request.snapshot_cache;
    Data_source& data_source = *request.data_source;
    const Data_access_policy& access = *request.access;
    const std::vector<std::size_t>& scales = *request.scales;
    const auto& get_timestamp = access.get_timestamp;

    if (scales.empty() || request.t_max_ns <= request.t_min_ns || request.width_px <= 0.0) {
        return plan;
    }

    if (snapshot_cache.cached_snapshot_frame_id != request.frame_id) {
        reset_snapshot_cache(snapshot_cache);
    }

    const std::size_t level_count = scales.size();
    const std::size_t max_level_index = level_count > 0 ? level_count - 1 : 0;
    std::size_t target_level = std::min<std::size_t>(
        state.last_lod_level,
        max_level_index);

    constexpr std::size_t k_tried_stack_levels = 32;
    std::array<std::uint8_t, k_tried_stack_levels> tried_stack{};
    std::vector<std::uint8_t> tried_heap;
    std::uint8_t* tried = nullptr;
    if (level_count <= k_tried_stack_levels) {
        tried = tried_stack.data();
        std::fill(tried, tried + level_count, std::uint8_t{0});
    }
    else {
        tried_heap.assign(level_count, std::uint8_t{0});
        tried = tried_heap.data();
    }

    const auto acquire_frame_snapshot = [&](std::size_t level) {
        VNM_PLOT_PROFILE_SCOPE(request.profiler, "process_view.try_snapshot");
        snapshot_result_t snapshot_result;
        if (snapshot_cache.cached_snapshot_frame_id == request.frame_id &&
            snapshot_cache.cached_snapshot_level == level &&
            snapshot_cache.cached_snapshot_source == &data_source &&
            snapshot_cache.cached_snapshot)
        {
            snapshot_result.snapshot = snapshot_cache.cached_snapshot;
            snapshot_result.status = snapshot_result_t::Snapshot_status::READY;
            return snapshot_result;
        }

        snapshot_result = data_source.try_snapshot(level);
        if (snapshot_result) {
            snapshot_cache.cached_snapshot_frame_id = request.frame_id;
            snapshot_cache.cached_snapshot_level = level;
            snapshot_cache.cached_snapshot_source = &data_source;
            snapshot_cache.cached_snapshot = snapshot_result.snapshot;
            snapshot_cache.cached_snapshot_hold = snapshot_result.snapshot.hold;
        }
        return snapshot_result;
    };

    const auto was_tried = [&](std::size_t level) -> bool {
        return tried && level < level_count && tried[level] != 0;
    };
    const auto mark_tried = [&](std::size_t level) {
        if (tried && level < level_count) {
            tried[level] = 1;
        }
    };

    const auto load_cached_plan = [&](Series_view_plan& p, std::size_t level) {
        p.source_first = state.last_first > 0
            ? static_cast<std::size_t>(state.last_first)
            : 0;
        const std::size_t gpu_count = state.last_count > 0
            ? static_cast<std::size_t>(state.last_count)
            : 0;
        p.synthetic_hold_count =
            state.last_hold_last_forward && gpu_count > 0 ? 1 : 0;
        p.source_count = gpu_count - p.synthetic_hold_count;
        p.gpu_count = gpu_count;
        p.lod_level = level;
        p.lod_scale = level < scales.size() ? scales[level] : std::size_t{1};
        p.pixels_per_sample = state.last_applied_pps;
        p.snapshot.sequence = state.last_sequence;
        p.t_min_ns = request.t_min_ns;
        p.t_max_ns = request.t_max_ns;
        p.t_origin_ns = request.t_origin_ns;
        p.hold_last_forward = state.last_hold_last_forward;
        p.hold_timestamp_ns = state.last_hold_last_forward ? request.t_max_ns : 0;
        p.width_px = static_cast<float>(request.width_px);
        p.interpolation = request.interpolation;
    };

    const auto try_stale_fallback = [&](Series_view_plan& p) -> bool {
        const void* current_identity = data_source.identity();
        // The cached VBO holds samples rebased against uploaded_t_origin_ns;
        // reusing it under a moved origin would draw at the wrong x positions
        // because set_common_uniforms feeds the new view_origin_ns regardless.
        const bool identity_ok =
            (state.cached_data_identity != nullptr) &&
            (state.cached_data_identity == current_identity) &&
            request.has_uploaded_vbo &&
            (state.last_count > 0) &&
            (state.last_empty_window_behavior == request.empty_window_behavior) &&
            (state.last_interpolation == request.interpolation) &&
            (state.uploaded_t_origin_ns == request.t_origin_ns);
        if (!identity_ok) {
            return false;
        }
        load_cached_plan(p, state.last_lod_level);
        return true;
    };

    const int max_attempts = static_cast<int>(level_count) + 2;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const std::size_t applied_level =
            std::min<std::size_t>(target_level, max_level_index);
        if (was_tried(applied_level)) {
            break;
        }
        mark_tried(applied_level);
        const std::size_t applied_scale = scales[applied_level];
        bool hold_last_forward = false;

        const std::uint64_t current_seq = data_source.current_sequence(applied_level);
        if (current_seq != 0 &&
            current_seq == state.last_sequence &&
            applied_level == state.last_lod_level &&
            request.has_uploaded_vbo &&
            state.last_count > 0 &&
            state.cached_data_identity == data_source.identity() &&
            state.last_t_min == request.t_min_ns &&
            state.last_t_max == request.t_max_ns &&
            state.last_width_px == request.width_px &&
            state.last_empty_window_behavior == request.empty_window_behavior &&
            state.last_interpolation == request.interpolation &&
            state.uploaded_t_origin_ns == request.t_origin_ns)
        {
            if (request.snapshot_requirement ==
                Snapshot_requirement::Frame_snapshot_required)
            {
                snapshot_result_t snapshot_result = acquire_frame_snapshot(applied_level);
                if (snapshot_result) {
                    if (snapshot_result.snapshot.sequence == current_seq) {
                        load_cached_plan(plan, applied_level);
                        plan.snapshot.snapshot = snapshot_result.snapshot;
                        plan.snapshot.sequence = snapshot_result.snapshot.sequence;
                        return plan;
                    }
                    // The source advanced between current_sequence() and
                    // try_snapshot(); fall through to replan against the
                    // newer frame-scoped snapshot instead of pairing cached
                    // first/count metadata with a different snapshot.
                }
                else {
                    load_cached_plan(plan, applied_level);
                    return plan;
                }
            }
            else {
                load_cached_plan(plan, applied_level);
                return plan;
            }
        }

        snapshot_result_t snapshot_result = acquire_frame_snapshot(applied_level);

        if (!snapshot_result || !snapshot_result.snapshot || snapshot_result.snapshot.count == 0) {
            if (try_stale_fallback(plan)) {
                break;
            }
            if (applied_level > 0) {
                target_level = applied_level - 1;
                continue;
            }
            break;
        }

        const auto& snapshot = snapshot_result.snapshot;
        bool timestamps_monotonic = true;
        if (get_timestamp) {
            const void* current_identity = data_source.identity();
            const bool need_monotonicity_scan =
                state.last_timestamp_order_sequence != snapshot.sequence ||
                state.last_timestamp_order_identity != current_identity;
            if (need_monotonicity_scan) {
                bool is_monotonic = true;
                const void* first_sample = snapshot.at(0);
                if (!first_sample) {
                    is_monotonic = false;
                }
                else {
                    std::int64_t prev_ts = get_timestamp(first_sample);
                    for (std::size_t i = 1; i < snapshot.count; ++i) {
                        const void* sample = snapshot.at(i);
                        if (!sample) {
                            is_monotonic = false;
                            break;
                        }
                        const std::int64_t ts = get_timestamp(sample);
                        if (ts < prev_ts) {
                            is_monotonic = false;
                            break;
                        }
                        prev_ts = ts;
                    }
                }
                state.last_timestamp_order_sequence = snapshot.sequence;
                state.last_timestamp_order_identity = current_identity;
                state.last_timestamps_monotonic = is_monotonic;
            }
            timestamps_monotonic = state.last_timestamps_monotonic;
        }

        std::size_t first_idx = 0;
        std::size_t last_idx = snapshot.count;
        std::int64_t last_ts = 0;
        bool have_last_ts = false;
        if (get_timestamp) {
            const void* last_sample = snapshot.at(snapshot.count - 1);
            if (last_sample) {
                last_ts = get_timestamp(last_sample);
                have_last_ts = true;
            }
            if (!timestamps_monotonic) {
                // Non-monotonic timestamps invalidate binary-search assumptions.
                // Fall back to a linear scan for correctness.
                VNM_PLOT_PROFILE_SCOPE(
                    request.profiler,
                    "process_view.linear_fallback");
                std::size_t match_first = snapshot.count;
                std::size_t match_last = 0;
                for (std::size_t i = 0; i < snapshot.count; ++i) {
                    const void* sample = snapshot.at(i);
                    if (!sample) {
                        continue;
                    }
                    const std::int64_t ts = get_timestamp(sample);
                    if (ts < request.t_min_ns || ts > request.t_max_ns) {
                        continue;
                    }
                    if (match_first == snapshot.count) {
                        match_first = i;
                    }
                    match_last = i + 1;
                }
                if (match_first < match_last) {
                    first_idx = (match_first > 0) ? (match_first - 1) : 0;
                    last_idx = std::min(match_last + 2, snapshot.count);
                }
                else {
                    first_idx = snapshot.count;
                    last_idx = snapshot.count;
                }
            }
            else {
                VNM_PLOT_PROFILE_SCOPE(
                    request.profiler,
                    "process_view.binary_search");
                first_idx = lower_bound_timestamp(
                    snapshot,
                    get_timestamp,
                    request.t_min_ns);
                if (first_idx > 0) {
                    --first_idx;
                }
                last_idx = upper_bound_timestamp(
                    snapshot,
                    get_timestamp,
                    request.t_max_ns);
                last_idx = std::min(last_idx + 2, snapshot.count);
            }
        }

        const bool can_hold_last_forward =
            request.empty_window_behavior ==
                Empty_window_behavior::HOLD_LAST_FORWARD &&
            access.clone_with_timestamp &&
            have_last_ts &&
            last_ts < request.t_max_ns;

        if (first_idx >= last_idx) {
            if (can_hold_last_forward) {
                first_idx = snapshot.count - 1;
                last_idx = snapshot.count;
                hold_last_forward = true;
            }
            else
            if (applied_level > 0 && !was_tried(applied_level - 1)) {
                target_level = applied_level - 1;
                continue;
            }
            else {
                break;
            }
        }
        else
        if (can_hold_last_forward && last_idx == snapshot.count) {
            hold_last_forward = true;
        }

        std::int32_t count = static_cast<std::int32_t>(last_idx - first_idx);
        if (hold_last_forward) {
            ++count;
        }
        const std::size_t base_samples = (count > 0)
            ? static_cast<std::size_t>(count) * applied_scale : 0;
        const double base_pps = (base_samples > 0)
            ? request.width_px / static_cast<double>(base_samples) : 0.0;

        const std::size_t desired_level = choose_lod_level(scales, base_pps);
        if (desired_level != applied_level) {
            if (!was_tried(desired_level)) {
                target_level = desired_level;
                continue;
            }
        }

        state.last_sequence = snapshot.sequence;
        state.cached_data_identity = data_source.identity();
        state.uploaded_t_origin_ns = request.t_origin_ns;
        state.last_snapshot_elements = snapshot.count;
        state.last_first = static_cast<std::int32_t>(first_idx);
        state.last_count = count;

        state.last_lod_level = applied_level;
        state.last_t_min = request.t_min_ns;
        state.last_t_max = request.t_max_ns;
        state.last_width_px = request.width_px;
        state.last_empty_window_behavior = request.empty_window_behavior;
        state.last_interpolation = request.interpolation;

        plan.source_first = state.last_first > 0
            ? static_cast<std::size_t>(state.last_first)
            : 0;
        const std::size_t gpu_count = state.last_count > 0
            ? static_cast<std::size_t>(state.last_count)
            : 0;
        plan.synthetic_hold_count = hold_last_forward && gpu_count > 0 ? 1 : 0;
        plan.source_count = gpu_count - plan.synthetic_hold_count;
        plan.gpu_count = gpu_count;
        plan.lod_level = applied_level;
        plan.lod_scale = applied_scale;
        plan.pixels_per_sample = base_pps * static_cast<double>(applied_scale);
        state.last_applied_pps = plan.pixels_per_sample;
        state.last_hold_last_forward = hold_last_forward;
        plan.snapshot.snapshot = snapshot;
        plan.snapshot.sequence = snapshot.sequence;
        plan.t_min_ns = request.t_min_ns;
        plan.t_max_ns = request.t_max_ns;
        plan.t_origin_ns = request.t_origin_ns;
        plan.hold_last_forward = hold_last_forward;
        plan.hold_timestamp_ns = hold_last_forward ? request.t_max_ns : 0;
        plan.width_px = static_cast<float>(request.width_px);
        plan.interpolation = request.interpolation;
        break;
    }

    return plan;
}

} // namespace vnm::plot::detail
