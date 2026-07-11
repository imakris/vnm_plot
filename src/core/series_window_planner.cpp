#include "series_window_planner.h"

#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/plot_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace vnm::plot::detail {

namespace {

void reset_snapshot_cache(Series_window_snapshot_cache& cache)
{
    cache = Series_window_snapshot_cache{};
}

bool checked_size_add(std::size_t lhs, std::size_t rhs, std::size_t& out)
{
    if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
        return false;
    }
    out = lhs + rhs;
    return true;
}

bool checked_size_product(std::size_t lhs, std::size_t rhs, std::size_t& out)
{
    if (rhs != 0 && lhs > std::numeric_limits<std::size_t>::max() / rhs) {
        return false;
    }
    out = lhs * rhs;
    return true;
}

struct drawable_window_result_t
{
    std::size_t    source_first         = 0;
    std::size_t    source_count         = 0;
    std::size_t    synthetic_hold_count = 0;
    std::size_t    gpu_count            = 0;
    std::vector<drawable_sample_span_t>
                   spans;
    bool           valid                = true;
};

struct direct_time_window_query_t
{
    data_query_result_t<sample_index_window_t>
                   result;
    bool           attempted            = false;
};

drawable_window_result_t build_drawable_window(
    const data_snapshot_t&         snapshot,
    const erased_access_policy_t&  access,
    Nonfinite_sample_policy        nonfinite_policy,
    std::size_t                    source_first,
    std::size_t                    source_last_exclusive,
    bool                           hold_last_forward)
{
    drawable_window_result_t result;
    result.source_first = source_first;
    result.source_count = source_last_exclusive > source_first
        ? source_last_exclusive - source_first
        : 0;

    if (source_first >= source_last_exclusive) {
        return result;
    }

    const auto append_drawable_source = [&](std::size_t source_index) -> bool {
        const void* sample = snapshot.at(source_index);
        sample_draw_value_t ignored;
        const sample_draw_status_t status = read_sample_draw_value(
            access,
            sample,
            nonfinite_policy,
            ignored);
        if (status == sample_draw_status_t::FAILED)  { return false; }
        if (status == sample_draw_status_t::SKIPPED) { return true;  }

        if (result.spans.empty() ||
            result.spans.back().source_first +
                result.spans.back().source_count != source_index)
        {
            drawable_sample_span_t span;
            span.source_first = source_index;
            span.gpu_first    = result.gpu_count;
            result.spans.push_back(span);
        }

        drawable_sample_span_t& span = result.spans.back();
        ++span.source_count;
        ++span.gpu_count;
        ++result.gpu_count;
        return true;
    };

    for (std::size_t i = source_first; i < source_last_exclusive; ++i) {
        if (!append_drawable_source(i)) {
            result.valid = false;
            return result;
        }
    }

    if (hold_last_forward) {
        const bool final_source_sample_drawable =
            !result.spans.empty() &&
            result.spans.back().source_first +
                result.spans.back().source_count == source_last_exclusive;
        const bool can_hold_skipping_trailing_invalid =
            nonfinite_policy == Nonfinite_sample_policy::SKIP &&
            !result.spans.empty();
        if (final_source_sample_drawable ||
            can_hold_skipping_trailing_invalid)
        {
            ++result.spans.back().gpu_count;
            ++result.gpu_count;
            result.synthetic_hold_count = 1;
        }
    }

    return result;
}

bool select_hold_source_index(
    const data_snapshot_t&         snapshot,
    const erased_access_policy_t&  access,
    Nonfinite_sample_policy        nonfinite_policy,
    std::size_t                    candidate_index,
    std::size_t&                   out_index,
    bool&                          out_failed)
{
    out_failed = false;
    if (candidate_index >= snapshot.count) {
        out_failed = true;
        return false;
    }

    const auto status_at = [&](std::size_t index) {
        const void* sample = snapshot.at(index);
        sample_draw_value_t ignored;
        return read_sample_draw_value(
            access,
            sample,
            nonfinite_policy,
            ignored);
    };

    if (nonfinite_policy == Nonfinite_sample_policy::SKIP) {
        for (std::size_t offset = candidate_index + 1u; offset > 0; --offset) {
            const std::size_t          index  = offset - 1u;
            const sample_draw_status_t status = status_at(index);
            if (status == sample_draw_status_t::DRAWABLE) {
                out_index = index;
                return true;
            }
            if (status == sample_draw_status_t::FAILED) {
                out_failed = true;
                return false;
            }
        }
        return false;
    }

    const sample_draw_status_t status = status_at(candidate_index);
    if (status == sample_draw_status_t::DRAWABLE) {
        out_index = candidate_index;
        return true;
    }
    if (status == sample_draw_status_t::FAILED) {
        out_failed = true;
    }
    return false;
}

} // anonymous namespace

Series_view_plan plan_series_window(const series_window_plan_request_t& request)
{
    Series_view_plan plan;
    plan.series_id             = request.series_id;
    plan.view_kind             = request.view_kind;
    plan.source                = request.data_source;
    plan.access                = request.access;
    plan.lod_scale             = 1;
    plan.interpolation         = request.interpolation;
    plan.empty_window_behavior = request.empty_window_behavior;
    plan.nonfinite_policy      = request.nonfinite_policy;
    plan.style                 = request.style;

    if (request.planner_state) {
        request.planner_state->last_plan_reused_upload = false;
    }

    if (!request.planner_state ||
        !request.snapshot_cache ||
        !request.data_source ||
        !request.access ||
        !request.scales)
    {
        return plan;
    }

    auto&        state          = *request.planner_state;
    auto&        snapshot_cache = *request.snapshot_cache;
    Data_source& data_source    = *request.data_source;

    const Data_access_policy& access = *request.access;
    const erased_access_policy_t access_view =
        make_erased_access_policy_view(access);
    const access_policy_cache_key_t access_key =
        make_access_policy_cache_key(&access, access_view);
    state.last_access_dispatch_kind = access_view.dispatch_kind;
    const std::vector<std::size_t>& scales = *request.scales;
    const auto get_timestamp = [&access_view](const void* sample) {
        return access_view.timestamp(sample);
    };

    if (scales.empty() || request.t_max_ns <= request.t_min_ns || request.width_px <= 0.0) {
        return plan;
    }

    if (snapshot_cache.cached_snapshot_frame_id != request.frame_id) {
        reset_snapshot_cache(snapshot_cache);
    }

    const std::size_t level_count     = scales.size();
    const std::size_t max_level_index = level_count > 0 ? level_count - 1 : 0;
    std::size_t target_level = std::min<std::size_t>(
        state.has_last_lod_level ? state.last_lod_level : 0,
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
            snapshot_cache.cached_snapshot_level    == level            &&
            snapshot_cache.cached_snapshot_source   == &data_source     &&
            snapshot_cache.cached_snapshot)
        {
            snapshot_result.snapshot = snapshot_cache.cached_snapshot;
            snapshot_result.status   = snapshot_result_t::Snapshot_status::READY;
            return snapshot_result;
        }

        snapshot_result = data_source.try_snapshot(level);
        if (snapshot_result) {
            snapshot_cache.cached_snapshot_frame_id = request.frame_id;
            snapshot_cache.cached_snapshot_level    = level;
            snapshot_cache.cached_snapshot_source   = &data_source;
            snapshot_cache.cached_snapshot          = snapshot_result.snapshot;
            snapshot_cache.cached_snapshot_hold     = snapshot_result.snapshot.hold;
        }
        return snapshot_result;
    };

    const auto make_query_context = [&]() {
        data_query_context_t query;
        query.access                = &access;
        query.profiler              = request.profiler;
        query.semantics_key         = make_sample_semantics_key(&access);
        query.time_window           = {request.t_min_ns, request.t_max_ns};
        query.interpolation         = request.interpolation;
        query.empty_window_behavior = request.empty_window_behavior;
        query.nonfinite_policy      = request.nonfinite_policy;
        return query;
    };

    const auto query_direct_time_window = [&](std::size_t level) {
        direct_time_window_query_t query;
        if (!data_source.supports_direct_time_window_query(level)) {
            return query;
        }
        VNM_PLOT_PROFILE_SCOPE(
            request.profiler,
            "process_view.query_time_window");
        query.result    = data_source.query_time_window(level, make_query_context());
        query.attempted = true;
        return query;
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
        p.source_first         = state.last_first;
        p.source_count         = state.last_source_count;
        p.synthetic_hold_count = state.last_synthetic_hold_count;
        p.gpu_count            = state.last_count;
        p.drawable_spans       = state.last_drawable_spans;
        p.lod_level            = level;
        p.lod_scale            = level < scales.size() ? scales[level] : std::size_t{1};
        p.pixels_per_sample    = state.last_applied_pps;
        p.snapshot.sequence    = state.last_sequence;
        p.t_min_ns             = request.t_min_ns;
        p.t_max_ns             = request.t_max_ns;
        p.t_origin_ns          = request.t_origin_ns;
        p.hold_last_forward    = state.last_hold_last_forward;
        p.hold_timestamp_ns    = state.last_hold_last_forward ? request.t_max_ns : 0;
        p.width_px             = static_cast<float>(request.width_px);
        p.interpolation        = request.interpolation;
        p.nonfinite_policy     = request.nonfinite_policy;
    };

    const auto try_stale_fallback =
        [&](Series_view_plan& p, std::size_t level) -> bool {
            const void* current_identity = data_source.identity();
            // The cached VBO holds samples rebased against uploaded_t_origin_ns;
            // reusing it under a moved origin would draw at the wrong x positions
            // because set_common_uniforms feeds the new view_origin_ns regardless.
            const bool identity_ok =
                (state.cached_data_identity != nullptr)                             &&
                (state.cached_data_identity == current_identity)                    &&
                request.has_uploaded_vbo                                            &&
                (state.last_count > 0)                                              &&
                state.has_last_lod_level                                            &&
                (state.last_lod_level == level)                                     &&
                (state.last_access_key == access_key)                               &&
                (state.last_t_min == request.t_min_ns)                              &&
                (state.last_t_max == request.t_max_ns)                              &&
                (state.last_width_px == request.width_px)                           &&
                (state.last_empty_window_behavior == request.empty_window_behavior) &&
                (state.last_nonfinite_policy == request.nonfinite_policy)           &&
                (state.last_interpolation == request.interpolation)                 &&
                (state.uploaded_t_origin_ns == request.t_origin_ns);
            if (!identity_ok) {
                return false;
            }
            load_cached_plan(p, state.last_lod_level);
            state.last_plan_reused_upload = true;
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
        const std::size_t applied_scale     = scales[applied_level];
        bool              hold_last_forward = false;

        const std::uint64_t current_seq = data_source.current_sequence(applied_level);
        if (current_seq                      != 0                             &&
            current_seq                      == state.last_sequence           &&
            applied_level                    == state.last_lod_level          &&
            request.has_uploaded_vbo                                          &&
            state.last_count                 >  0                             &&
            state.cached_data_identity       == data_source.identity()        &&
            state.last_access_key            == access_key                    &&
            state.last_t_min                 == request.t_min_ns              &&
            state.last_t_max                 == request.t_max_ns              &&
            state.last_width_px              == request.width_px              &&
            state.last_empty_window_behavior == request.empty_window_behavior &&
            state.last_nonfinite_policy      == request.nonfinite_policy      &&
            state.last_interpolation         == request.interpolation         &&
            state.uploaded_t_origin_ns       == request.t_origin_ns)
        {
            if (request.snapshot_requirement ==
                Snapshot_requirement::Frame_snapshot_required)
            {
                snapshot_result_t snapshot_result = acquire_frame_snapshot(applied_level);
                if (snapshot_result) {
                    if (snapshot_result.snapshot.sequence == current_seq) {
                        load_cached_plan(plan, applied_level);
                        plan.snapshot.snapshot        = snapshot_result.snapshot;
                        plan.snapshot.sequence        = snapshot_result.snapshot.sequence;
                        state.last_plan_reused_upload = true;
                        return plan;
                    }
                    // The source advanced between current_sequence() and
                    // try_snapshot(); fall through to replan against the
                    // newer frame-scoped snapshot instead of pairing cached
                    // window metadata with a different snapshot.
                }
                else {
                    load_cached_plan(plan, applied_level);
                    state.last_plan_reused_upload = true;
                    return plan;
                }
            }
            else {
                load_cached_plan(plan, applied_level);
                state.last_plan_reused_upload = true;
                return plan;
            }
        }

        const direct_time_window_query_t direct_time_window =
            query_direct_time_window(applied_level);

        snapshot_result_t snapshot_result = acquire_frame_snapshot(applied_level);

        if (!snapshot_result || !snapshot_result.snapshot || snapshot_result.snapshot.count == 0) {
            if (try_stale_fallback(plan, applied_level)) {
                break;
            }
            if (applied_level > 0) {
                target_level = applied_level - 1;
                continue;
            }
            break;
        }

        const auto& snapshot                  = snapshot_result.snapshot;
        bool        have_direct_time_window   = false;
        bool        direct_time_window_empty  = false;
        bool        direct_time_window_failed = false;
        std::size_t direct_first_idx          = 0;
        std::size_t direct_last_idx           = 0;
        bool        direct_hold_last_forward  = false;
        if (direct_time_window.attempted &&
            direct_time_window.result.sequence != 0 &&
            direct_time_window.result.sequence == snapshot.sequence)
        {
            if (direct_time_window.result.status == Data_query_status::EMPTY) {
                // A semantic query can be empty while renderer interpolation
                // still needs adjacent source samples to draw a clipped
                // segment. Fall back to local padded snapshot search.
            }
            else
            if (direct_time_window.result.status == Data_query_status::READY) {
                const std::size_t first          = direct_time_window.result.value.first;
                const std::size_t count          = direct_time_window.result.value.count;
                std::size_t       last_exclusive = first;
                if (count == 0) {
                    have_direct_time_window = true;
                    direct_time_window_empty = true;
                }
                else
                if (first          <  snapshot.count               &&
                    checked_size_add(first, count, last_exclusive) &&
                    last_exclusive <= snapshot.count)
                {
                    have_direct_time_window = true;
                    direct_first_idx        = first;
                    direct_last_idx         = last_exclusive;
                    if (access_view.has_timestamp()) {
                        bool has_match_in_requested_window = false;
                        bool direct_window_valid           = true;
                        for (std::size_t index = first;
                            index < last_exclusive;
                            ++index)
                        {
                            const void* sample = snapshot.at(index);
                            if (!sample) {
                                direct_window_valid = false;
                                break;
                            }
                            const std::int64_t ts_ns = get_timestamp(sample);
                            if (ts_ns >= request.t_min_ns &&
                                ts_ns <= request.t_max_ns)
                            {
                                has_match_in_requested_window = true;
                                break;
                            }
                        }
                        if (!direct_window_valid) {
                            have_direct_time_window = false;
                            direct_time_window_failed = true;
                        }
                        else
                        if (has_match_in_requested_window) {
                            const void* first_sample = snapshot.at(first);
                            if (!first_sample) {
                                have_direct_time_window = false;
                                direct_time_window_failed = true;
                            }
                            else {
                                const std::int64_t first_ts_ns =
                                    get_timestamp(first_sample);
                                if (first_ts_ns      >= request.t_min_ns &&
                                    direct_first_idx >  0)
                                {
                                    --direct_first_idx;
                                }
                                std::size_t padded_last = direct_last_idx;
                                if (checked_size_add(
                                        direct_last_idx, 2u, padded_last))
                                {
                                    direct_last_idx =
                                        std::min(padded_last, snapshot.count);
                                }
                                else {
                                    direct_last_idx = snapshot.count;
                                }
                                if (request.empty_window_behavior ==
                                    Empty_window_behavior::HOLD_LAST_FORWARD &&
                                    direct_last_idx == snapshot.count)
                                {
                                    const void* last_window_sample =
                                        snapshot.at(direct_last_idx - 1u);
                                    if (!last_window_sample) {
                                        have_direct_time_window = false;
                                        direct_time_window_failed = true;
                                    }
                                    else
                                    if (get_timestamp(last_window_sample) <
                                        request.t_max_ns)
                                    {
                                        direct_hold_last_forward = true;
                                    }
                                }
                            }
                        }
                        else
                        if (request.interpolation ==
                            Series_interpolation::STEP_AFTER &&
                            request.empty_window_behavior ==
                            Empty_window_behavior::HOLD_LAST_FORWARD)
                        {
                            const void* last_window_sample =
                                snapshot.at(last_exclusive - 1u);
                            if (!last_window_sample) {
                                have_direct_time_window = false;
                                direct_time_window_failed = true;
                            }
                            else
                            if (get_timestamp(last_window_sample) <
                                request.t_max_ns)
                            {
                                direct_hold_last_forward = true;
                            }
                        }
                    }
                }
                else {
                    direct_time_window_failed = true;
                }
            }
            else
            if (direct_time_window.result.status == Data_query_status::FAILED) {
                direct_time_window_failed = true;
            }
        }

        bool timestamps_monotonic = true;
        state.last_timestamp_order_scan_performed = false;
        state.last_timestamp_order_scan_samples   = 0;
        state.last_timestamp_window_search        = Timestamp_window_search::NONE;
        if (direct_time_window_failed) {
            state.last_timestamp_window_search =
                Timestamp_window_search::QUERY;
            break;
        }
        if (have_direct_time_window) {
            state.last_timestamp_window_search =
                Timestamp_window_search::QUERY;
        }
        else
        if (access_view.has_timestamp()) {
            const void*      current_identity = data_source.identity();
            const Time_order source_order     = data_source.time_order(applied_level);
            if (source_order == Time_order::ASCENDING) {
                state.last_timestamp_order_sequence   = snapshot.sequence;
                state.last_timestamp_order_identity   = current_identity;
                state.last_timestamp_order_access_key = access_key;
                state.last_timestamp_source_order     = source_order;
                state.last_timestamps_monotonic       = true;
            }
            else
            if (source_order == Time_order::DESCENDING) {
                state.last_timestamp_order_sequence   = snapshot.sequence;
                state.last_timestamp_order_identity   = current_identity;
                state.last_timestamp_order_access_key = access_key;
                state.last_timestamp_source_order     = source_order;
                state.last_timestamps_monotonic       = false;
            }
            else {
                const bool need_monotonicity_scan =
                    state.last_timestamp_order_sequence   != snapshot.sequence ||
                    state.last_timestamp_order_identity   != current_identity  ||
                    state.last_timestamp_order_access_key != access_key        ||
                    state.last_timestamp_source_order     != source_order;
                if (need_monotonicity_scan) {
                    bool is_monotonic = true;
                    state.last_timestamp_order_scan_performed = true;
                    const void* first_sample = snapshot.at(0);
                    ++state.last_timestamp_order_scan_samples;
                    if (!first_sample) {
                        is_monotonic = false;
                    }
                    else {
                        std::int64_t prev_ts = get_timestamp(first_sample);
                        for (std::size_t i = 1; i < snapshot.count; ++i) {
                            const void* sample = snapshot.at(i);
                            ++state.last_timestamp_order_scan_samples;
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
                    state.last_timestamp_order_sequence   = snapshot.sequence;
                    state.last_timestamp_order_identity   = current_identity;
                    state.last_timestamp_order_access_key = access_key;
                    state.last_timestamp_source_order     = source_order;
                    state.last_timestamps_monotonic       = is_monotonic;
                    if (request.profiler) {
                        request.profiler->record_counter(
                            "renderer.series_window.monotonicity_scan_count");
                        request.profiler->record_observation(
                            "renderer.series_window.monotonicity_scan_samples",
                            static_cast<double>(
                                state.last_timestamp_order_scan_samples));
                    }
                }
            }
            timestamps_monotonic = state.last_timestamps_monotonic;
        }

        std::size_t  first_idx    = 0;
        std::size_t  last_idx     = snapshot.count;
        std::int64_t last_ts      = 0;
        bool         have_last_ts = false;
        if (have_direct_time_window) {
            if (direct_time_window_empty) {
                first_idx = snapshot.count;
                last_idx = snapshot.count;
            }
            else {
                first_idx         = direct_first_idx;
                last_idx          = direct_last_idx;
                hold_last_forward = direct_hold_last_forward;
            }
        }
        else
        if (access_view.has_timestamp()) {
            const void* last_sample = snapshot.at(snapshot.count - 1);
            if (last_sample) {
                last_ts = get_timestamp(last_sample);
                have_last_ts = true;
            }
            if (!timestamps_monotonic) {
                // Non-monotonic timestamps invalidate binary-search assumptions.
                // Fall back to a linear scan for correctness.
                state.last_timestamp_window_search =
                    Timestamp_window_search::LINEAR;
                VNM_PLOT_PROFILE_SCOPE(
                    request.profiler,
                    "process_view.linear_fallback");
                const visible_sample_window_t window = select_visible_sample_window(
                    snapshot,
                    get_timestamp,
                    request.t_min_ns,
                    request.t_max_ns,
                    false);
                first_idx = window.first;
                last_idx = window.last_exclusive;
            }
            else {
                state.last_timestamp_window_search =
                    Timestamp_window_search::BINARY;
                VNM_PLOT_PROFILE_SCOPE(
                    request.profiler,
                    "process_view.binary_search");
                const visible_sample_window_t window = select_visible_sample_window(
                    snapshot,
                    get_timestamp,
                    request.t_min_ns,
                    request.t_max_ns,
                    true);
                first_idx = window.first;
                last_idx = window.last_exclusive;
            }
        }

        const bool can_hold_last_forward =
            request.empty_window_behavior ==
                Empty_window_behavior::HOLD_LAST_FORWARD &&
            !have_direct_time_window &&
            timestamps_monotonic &&
            have_last_ts &&
            last_ts < request.t_max_ns;

        if (first_idx >= last_idx) {
            if (can_hold_last_forward) {
                std::size_t hold_source_index = 0;
                bool        hold_failed       = false;
                if (select_hold_source_index(
                        snapshot, access_view, request.nonfinite_policy,
                        snapshot.count - 1u, hold_source_index, hold_failed))
                {
                    first_idx         = hold_source_index;
                    last_idx          = hold_source_index + 1u;
                    hold_last_forward = true;
                }
                else
                if (hold_failed) {
                    break;
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

        if (request.interpolation    == Series_interpolation::STEP_AFTER &&
            request.empty_window_behavior ==
                Empty_window_behavior::HOLD_LAST_FORWARD                 &&
            request.nonfinite_policy == Nonfinite_sample_policy::SKIP    &&
            !have_direct_time_window                                     &&
            timestamps_monotonic                                         &&
            first_idx                <  last_idx)
        {
            bool has_drawable_sample_in_requested_window = false;
            bool failed_sample_in_requested_window       = false;
            for (std::size_t index = first_idx; index < last_idx; ++index) {
                const void* sample = snapshot.at(index);
                if (!sample) {
                    failed_sample_in_requested_window = true;
                    break;
                }
                const std::int64_t ts_ns = get_timestamp(sample);
                if (ts_ns >= request.t_min_ns && ts_ns <= request.t_max_ns) {
                    sample_draw_value_t ignored;
                    const sample_draw_status_t status = read_sample_draw_value(
                        access_view,
                        sample,
                        request.nonfinite_policy,
                        ignored);
                    if (status == sample_draw_status_t::FAILED) {
                        failed_sample_in_requested_window = true;
                        break;
                    }
                    if (status == sample_draw_status_t::DRAWABLE) {
                        has_drawable_sample_in_requested_window = true;
                        break;
                    }
                }
            }
            if (failed_sample_in_requested_window) {
                break;
            }
            const void* first_sample = snapshot.at(first_idx);
            if (first_sample &&
                get_timestamp(first_sample) < request.t_min_ns)
            {
                std::size_t hold_source_index = 0;
                bool        hold_failed       = false;
                if (select_hold_source_index(
                        snapshot, access_view, request.nonfinite_policy, first_idx, hold_source_index, hold_failed))
                {
                    if (has_drawable_sample_in_requested_window) {
                        first_idx = std::min(first_idx, hold_source_index);
                    }
                    else {
                        first_idx         = hold_source_index;
                        last_idx          = hold_source_index + 1u;
                        hold_last_forward = true;
                    }
                }
                else
                if (hold_failed) {
                    break;
                }
                else
                if (!has_drawable_sample_in_requested_window) {
                    break;
                }
            }
        }

        drawable_window_result_t drawable_window = build_drawable_window(
            snapshot,
            access_view,
            request.nonfinite_policy,
            first_idx,
            last_idx,
            hold_last_forward);
        if (drawable_window.valid &&
            drawable_window.gpu_count == 0 &&
            hold_last_forward              &&
            request.nonfinite_policy  == Nonfinite_sample_policy::SKIP &&
            last_idx                  >  0)
        {
            std::size_t hold_source_index = 0;
            bool        hold_failed       = false;
            if (select_hold_source_index(
                    snapshot, access_view, request.nonfinite_policy, last_idx - 1u, hold_source_index, hold_failed))
            {
                first_idx       = hold_source_index;
                last_idx        = hold_source_index + 1u;
                drawable_window = build_drawable_window(
                    snapshot,
                    access_view,
                    request.nonfinite_policy,
                    first_idx,
                    last_idx,
                    hold_last_forward);
            }
            else
            if (hold_failed) {
                drawable_window.valid = false;
            }
        }
        if (!drawable_window.valid || drawable_window.gpu_count == 0) {
            break;
        }

        const std::size_t source_count  = last_idx - first_idx;
        std::size_t       count_for_lod = 0;
        if (!checked_size_add(
                source_count, hold_last_forward ? 1u : 0u, count_for_lod))
        {
            break;
        }
        std::size_t base_samples = 0;
        if (count_for_lod > 0 &&
            !checked_size_product(count_for_lod, applied_scale, base_samples))
        {
            break;
        }
        const double base_pps = (base_samples > 0)
            ? request.width_px / static_cast<double>(base_samples) : 0.0;

        const std::size_t desired_level = choose_lod_level(scales, base_pps);
        if (desired_level != applied_level) {
            if (!was_tried(desired_level)) {
                target_level = desired_level;
                continue;
            }
        }

        const bool lod_switched =
            state.has_last_lod_level                             &&
            state.cached_data_identity == data_source.identity() &&
            state.last_lod_level != applied_level;

        state.last_sequence          = snapshot.sequence;
        state.cached_data_identity   = data_source.identity();
        state.last_access_key        = access_key;
        state.uploaded_t_origin_ns   = request.t_origin_ns;
        state.last_snapshot_elements = snapshot.count;
        state.last_first             = first_idx;
        state.last_count             = drawable_window.gpu_count;

        state.last_lod_level             = applied_level;
        state.has_last_lod_level         = true;
        state.last_t_min                 = request.t_min_ns;
        state.last_t_max                 = request.t_max_ns;
        state.last_width_px              = request.width_px;
        state.last_empty_window_behavior = request.empty_window_behavior;
        state.last_nonfinite_policy      = request.nonfinite_policy;
        state.last_interpolation         = request.interpolation;
        if (lod_switched && request.profiler) {
            request.profiler->record_counter(
                "renderer.series_window.lod_switch_count");
        }

        const bool effective_hold_last_forward =
            drawable_window.synthetic_hold_count > 0;

        plan.source_first            = state.last_first;
        plan.source_count            = drawable_window.source_count;
        plan.synthetic_hold_count    = drawable_window.synthetic_hold_count;
        plan.gpu_count               = drawable_window.gpu_count;
        plan.drawable_spans          = drawable_window.spans;
        plan.lod_level               = applied_level;
        plan.lod_scale               = applied_scale;
        plan.pixels_per_sample       = base_pps * static_cast<double>(applied_scale);
        state.last_applied_pps       = plan.pixels_per_sample;
        state.last_hold_last_forward = effective_hold_last_forward;
        plan.snapshot.snapshot       = snapshot;
        plan.snapshot.sequence       = snapshot.sequence;
        plan.t_min_ns                = request.t_min_ns;
        plan.t_max_ns                = request.t_max_ns;
        plan.t_origin_ns             = request.t_origin_ns;
        plan.hold_last_forward       = effective_hold_last_forward;
        plan.hold_timestamp_ns       = effective_hold_last_forward
            ? request.t_max_ns
            : 0;
        plan.width_px                = static_cast<float>(request.width_px);
        plan.interpolation           = request.interpolation;

        state.last_source_count         = plan.source_count;
        state.last_synthetic_hold_count = plan.synthetic_hold_count;
        state.last_drawable_spans       = plan.drawable_spans;
        break;
    }

    return plan;
}

Stack_rejection_reason compose_stacked_series(
    const std::vector<const Series_view_plan*>&    plans,
    std::vector<std::vector<stacked_sample_t>>&    layers)
{
    struct point_t { std::int64_t t; float v; };

    layers.clear();
    if (plans.size() < 2) {
        return Stack_rejection_reason::NO_DRAWABLE_DATA;
    }
    if (std::any_of(plans.begin(), plans.end(), [](const auto* plan) { return !plan; })) {
        return Stack_rejection_reason::NO_DRAWABLE_DATA;
    }
    const Series_interpolation interpolation = plans.front()->interpolation;
    if (std::any_of(plans.begin(), plans.end(),
        [interpolation](const auto* plan) { return plan->interpolation != interpolation; }))
    {
        return Stack_rejection_reason::MIXED_INTERPOLATION;
    }

    std::vector<std::vector<point_t>> points(plans.size());
    for (std::size_t layer = 0; layer < plans.size(); ++layer) {
        const Series_view_plan* plan = plans[layer];
        if (!plan->snapshot.snapshot || !plan->access || !plan->access->get_value)
        {
            return Stack_rejection_reason::NO_DRAWABLE_DATA;
        }
        if (plan->drawable_spans.size() != 1) {
            return Stack_rejection_reason::INCOMPATIBLE_DATA;
        }

        const drawable_sample_span_t& span = plan->drawable_spans.front();
        const auto access = make_erased_access_policy_view(*plan->access);

        auto& out = points[layer];
        out.reserve(span.gpu_count);
        for (std::size_t i = 0; i < span.source_count; ++i) {
            const void* sample = plan->snapshot.snapshot.at(span.source_first + i);
            sample_draw_value_t value;
            if (!sample || read_sample_draw_value(
                access, sample, plan->nonfinite_policy, value) !=
                sample_draw_status_t::DRAWABLE)
            {
                return Stack_rejection_reason::INCOMPATIBLE_DATA;
            }
            const std::int64_t timestamp = access.timestamp(sample);
            if (!out.empty() && out.back().t == timestamp) {
                out.back().v = value.y;
            }
            else {
                out.push_back({timestamp, value.y});
            }
        }
        if (span.gpu_count == span.source_count + 1u) {
            if (out.empty()) {
                return Stack_rejection_reason::NO_DRAWABLE_DATA;
            }
            if (out.back().t != plan->hold_timestamp_ns) {
                out.push_back({plan->hold_timestamp_ns, out.back().v});
            }
        }
        if (out.empty()) {
            return Stack_rejection_reason::NO_DRAWABLE_DATA;
        }
        if (out.size() > 1 && out.front().t > out.back().t) {
            std::reverse(out.begin(), out.end());
        }
        if (!std::is_sorted(out.begin(), out.end(),
            [](const point_t& a, const point_t& b) { return a.t < b.t; }))
        {
            return Stack_rejection_reason::NONMONOTONIC_TIMESTAMPS;
        }
    }

    std::vector<std::int64_t> timestamps;
    std::int64_t overlap_start = points.front().front().t;
    std::int64_t overlap_end   = points.front().back().t;
    for (const auto& source : points) {
        overlap_start = std::max(overlap_start, source.front().t);
        overlap_end   = std::min(overlap_end, source.back().t);
    }
    if (overlap_end < overlap_start) {
        return Stack_rejection_reason::NO_COMMON_DOMAIN;
    }

    std::vector<std::size_t> merge_indices(points.size(), 0);
    for (;;) {
        bool         found = false;
        std::int64_t next  = 0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (merge_indices[i] == points[i].size()) {
                continue;
            }
            const std::int64_t candidate = points[i][merge_indices[i]].t;
            if (!found || candidate < next) {
                next  = candidate;
                found = true;
            }
        }
        if (!found)                { break;                      }
        if (next > overlap_end)    { break;                      }
        if (next >= overlap_start) { timestamps.push_back(next); }
        for (std::size_t i = 0; i < points.size(); ++i) {
            while (merge_indices[i]           <  points[i].size() &&
                points[i][merge_indices[i]].t == next)
            {
                ++merge_indices[i];
            }
        }
    }

    layers.assign(plans.size(), {});
    for (auto& layer : layers) {
        layer.reserve(timestamps.size());
    }
    std::vector<std::size_t> cursors(points.size(), 0);
    for (const std::int64_t timestamp : timestamps) {
        float cumulative = 0.0f;
        for (std::size_t layer = 0; layer < points.size(); ++layer) {
            const auto& source = points[layer];
            while (cursors[layer] + 1u        <  source.size() &&
                source[cursors[layer] + 1u].t <= timestamp)
            {
                ++cursors[layer];
            }

            float value = source[cursors[layer]].v;
            if (plans[layer]->interpolation == Series_interpolation::LINEAR &&
                cursors[layer] + 1u         <  source.size()                &&
                timestamp                   >  source[cursors[layer]].t)
            {
                const point_t& a = source[cursors[layer]];
                const point_t& b = source[cursors[layer] + 1u];
                const double position =
                    normalized_time_position_ns(a.t, timestamp, b.t);
                value = static_cast<float>(a.v + position * (b.v - a.v));
            }

            const float base = cumulative;
            cumulative += value;
            if (!std::isfinite(cumulative)) {
                layers.clear();
                return Stack_rejection_reason::CUMULATIVE_OVERFLOW;
            }
            layers[layer].push_back({timestamp, cumulative, base});
        }
    }
    return timestamps.empty()
        ? Stack_rejection_reason::NO_DRAWABLE_DATA
        : Stack_rejection_reason::NONE;
}

const Data_access_policy& stacked_sample_access()
{
    static const Data_access_policy access = [] {
        Data_access_policy policy;
        policy.get_timestamp = [](const void* sample) {
            return static_cast<const stacked_sample_t*>(sample)->timestamp_ns;
        };
        policy.get_value = [](const void* sample) {
            return static_cast<const stacked_sample_t*>(sample)->value;
        };
        policy.get_range = [](const void* sample) {
            const auto& value = *static_cast<const stacked_sample_t*>(sample);
            return std::make_pair(value.base, value.value);
        };
        return policy;
    }();
    return access;
}

} // namespace vnm::plot::detail
