#include <vnm_plot/core/types.h>
#include <vnm_plot/core/plot_config.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace vnm::plot {

namespace {

enum class sample_scan_status
{
    ACCEPTED,
    SKIPPED,
    FAILED,
};

struct index_window_t
{
    std::size_t first = 0;
    std::size_t last_exclusive = 0;
    bool        valid = true;
};

struct time_window_candidates_t
{
    std::size_t match_first = 0;
    std::size_t match_last_exclusive = 0;
    std::size_t held_index = 0;
    bool        has_held = false;
    bool        valid = true;
};

Data_query_status status_from_snapshot(snapshot_result_t::Snapshot_status status)
{
    switch (status) {
    case snapshot_result_t::Snapshot_status::READY:
        return Data_query_status::READY;
    case snapshot_result_t::Snapshot_status::EMPTY:
        return Data_query_status::EMPTY;
    case snapshot_result_t::Snapshot_status::BUSY:
        return Data_query_status::BUSY;
    case snapshot_result_t::Snapshot_status::FAILED:
        return Data_query_status::FAILED;
    }
    return Data_query_status::FAILED;
}

Data_query_status invalid_ready_snapshot_status(const data_snapshot_t& snapshot)
{
    return snapshot.count == 0
        ? Data_query_status::EMPTY
        : Data_query_status::FAILED;
}

bool time_window_contains(time_range_t window, std::int64_t timestamp_ns)
{
    return window.min_ns <= window.max_ns
        && timestamp_ns >= window.min_ns
        && timestamp_ns <= window.max_ns;
}

bool wants_hold_forward(const data_query_context_t& query)
{
    return query.interpolation == Series_interpolation::STEP_AFTER
        && query.empty_window_behavior == Empty_window_behavior::HOLD_LAST_FORWARD;
}

bool timestamp_at(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    std::size_t index,
    std::int64_t& timestamp_ns)
{
    const void* sample = snapshot.at(index);
    if (!sample) {
        return false;
    }
    timestamp_ns = access.get_timestamp(sample);
    return true;
}

void include_value(value_range_t& range, bool& has_value, float value)
{
    if (!has_value) {
        range.min = value;
        range.max = value;
        has_value = true;
        return;
    }
    if (value < range.min) { range.min = value; }
    if (value > range.max) { range.max = value; }
}

void include_range(value_range_t& range, bool& has_value, float low, float high)
{
    if (high < low) {
        std::swap(low, high);
    }
    include_value(range, has_value, low);
    include_value(range, has_value, high);
}

sample_scan_status normalize_value(
    float& value,
    Nonfinite_sample_policy policy)
{
    if (std::isfinite(value)) {
        return sample_scan_status::ACCEPTED;
    }
    if (policy == Nonfinite_sample_policy::REPLACE_WITH_ZERO) {
        value = 0.0f;
        return sample_scan_status::ACCEPTED;
    }
    if (policy == Nonfinite_sample_policy::REJECT_WINDOW) {
        return sample_scan_status::FAILED;
    }

    return sample_scan_status::SKIPPED;
}

sample_scan_status normalize_range(
    float& low,
    float& high,
    Nonfinite_sample_policy policy)
{
    const bool low_finite = std::isfinite(low);
    const bool high_finite = std::isfinite(high);
    if (low_finite && high_finite) {
        return sample_scan_status::ACCEPTED;
    }
    if (policy == Nonfinite_sample_policy::REPLACE_WITH_ZERO) {
        if (!low_finite) { low = 0.0f; }
        if (!high_finite) { high = 0.0f; }
        return sample_scan_status::ACCEPTED;
    }
    if (policy == Nonfinite_sample_policy::REJECT_WINDOW) {
        return sample_scan_status::FAILED;
    }

    // BREAK_SEGMENT is a rendering distinction; aggregate range queries
    // skip the nonfinite sample just like SKIP.
    return sample_scan_status::SKIPPED;
}

sample_scan_status sample_value_range(
    const Data_access_policy& access,
    const void* sample,
    Nonfinite_sample_policy policy,
    value_range_t& range)
{
    if (access.get_range) {
        auto [low, high] = access.get_range(sample);
        const sample_scan_status status = normalize_range(low, high, policy);
        if (status != sample_scan_status::ACCEPTED) {
            return status;
        }
        if (high < low) {
            std::swap(low, high);
        }
        range = {low, high};
        return sample_scan_status::ACCEPTED;
    }

    if (access.get_value) {
        float value = access.get_value(sample);
        const sample_scan_status status = normalize_value(value, policy);
        if (status != sample_scan_status::ACCEPTED) {
            return status;
        }
        range = {value, value};
        return sample_scan_status::ACCEPTED;
    }

    return sample_scan_status::ACCEPTED;
}

sample_scan_status sample_status_for_staging(
    const Data_access_policy& access,
    const void* sample,
    Nonfinite_sample_policy policy)
{
    value_range_t ignored;
    return sample_value_range(access, sample, policy, ignored);
}

std::size_t ascending_first_ge(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    std::int64_t target_ns,
    bool& valid)
{
    std::size_t first = 0;
    std::size_t count = snapshot.count;
    while (count > 0) {
        const std::size_t step = count / 2;
        const std::size_t index = first + step;
        std::int64_t timestamp_ns = 0;
        if (!timestamp_at(snapshot, access, index, timestamp_ns)) {
            valid = false;
            return 0;
        }
        if (timestamp_ns < target_ns) {
            first = index + 1;
            count -= step + 1;
        }
        else {
            count = step;
        }
    }
    return first;
}

std::size_t ascending_first_gt(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    std::int64_t target_ns,
    bool& valid)
{
    std::size_t first = 0;
    std::size_t count = snapshot.count;
    while (count > 0) {
        const std::size_t step = count / 2;
        const std::size_t index = first + step;
        std::int64_t timestamp_ns = 0;
        if (!timestamp_at(snapshot, access, index, timestamp_ns)) {
            valid = false;
            return 0;
        }
        if (timestamp_ns <= target_ns) {
            first = index + 1;
            count -= step + 1;
        }
        else {
            count = step;
        }
    }
    return first;
}

std::size_t descending_first_le(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    std::int64_t target_ns,
    bool& valid)
{
    std::size_t first = 0;
    std::size_t count = snapshot.count;
    while (count > 0) {
        const std::size_t step = count / 2;
        const std::size_t index = first + step;
        std::int64_t timestamp_ns = 0;
        if (!timestamp_at(snapshot, access, index, timestamp_ns)) {
            valid = false;
            return 0;
        }
        if (timestamp_ns > target_ns) {
            first = index + 1;
            count -= step + 1;
        }
        else {
            count = step;
        }
    }
    return first;
}

std::size_t descending_first_lt(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    std::int64_t target_ns,
    bool& valid)
{
    std::size_t first = 0;
    std::size_t count = snapshot.count;
    while (count > 0) {
        const std::size_t step = count / 2;
        const std::size_t index = first + step;
        std::int64_t timestamp_ns = 0;
        if (!timestamp_at(snapshot, access, index, timestamp_ns)) {
            valid = false;
            return 0;
        }
        if (timestamp_ns >= target_ns) {
            first = index + 1;
            count -= step + 1;
        }
        else {
            count = step;
        }
    }
    return first;
}

time_window_candidates_t ascending_candidates(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    const data_query_context_t& query)
{
    bool valid = true;
    time_window_candidates_t out;
    out.match_first = ascending_first_ge(snapshot, access, query.time_window.min_ns, valid);
    if (!valid) {
        out.valid = false;
        return out;
    }
    out.match_last_exclusive = ascending_first_gt(snapshot, access, query.time_window.max_ns, valid);
    if (!valid) {
        out.valid = false;
        return out;
    }
    if (wants_hold_forward(query) && out.match_first > 0) {
        out.has_held = true;
        out.held_index = out.match_first - 1;
    }
    return out;
}

time_window_candidates_t descending_candidates(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    const data_query_context_t& query)
{
    bool valid = true;
    time_window_candidates_t out;
    out.match_first = descending_first_le(snapshot, access, query.time_window.max_ns, valid);
    if (!valid) {
        out.valid = false;
        return out;
    }
    out.match_last_exclusive = descending_first_lt(snapshot, access, query.time_window.min_ns, valid);
    if (!valid) {
        out.valid = false;
        return out;
    }
    if (wants_hold_forward(query) && out.match_last_exclusive < snapshot.count) {
        out.has_held = true;
        out.held_index = out.match_last_exclusive;
    }
    return out;
}

time_window_candidates_t linear_candidates(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    const data_query_context_t& query)
{
    bool found = false;
    bool gap_after_match = false;
    bool discontinuous = false;
    bool held_found = false;
    std::size_t held_index = 0;
    std::int64_t held_timestamp_ns = 0;
    time_window_candidates_t out;
    const bool hold_forward = wants_hold_forward(query);

    for (std::size_t index = 0; index < snapshot.count; ++index) {
        std::int64_t timestamp_ns = 0;
        if (!timestamp_at(snapshot, access, index, timestamp_ns)) {
            out.valid = false;
            return out;
        }

        if (time_window_contains(query.time_window, timestamp_ns)) {
            if (!found) {
                out.match_first = index;
                found = true;
            }
            else
            if (gap_after_match) {
                discontinuous = true;
            }
            out.match_last_exclusive = index + 1;
            gap_after_match = false;
        }
        else
        if (found) {
            gap_after_match = true;
        }

        if (hold_forward && timestamp_ns < query.time_window.min_ns &&
            (!held_found || timestamp_ns > held_timestamp_ns))
        {
            held_found = true;
            held_index = index;
            held_timestamp_ns = timestamp_ns;
        }
    }

    if (discontinuous) {
        out.valid = false;
        return out;
    }
    if (held_found) {
        if (found &&
            !(held_index + 1 == out.match_first || held_index == out.match_last_exclusive))
        {
            out.valid = false;
            return out;
        }
        out.has_held = true;
        out.held_index = held_index;
    }
    return out;
}

time_window_candidates_t time_window_candidates(
    const Data_source& source,
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    std::size_t lod,
    const data_query_context_t& query)
{
    switch (source.time_order(lod)) {
    case Time_order::ASCENDING:
        return ascending_candidates(snapshot, access, query);
    case Time_order::DESCENDING:
        return descending_candidates(snapshot, access, query);
    case Time_order::UNKNOWN:
    case Time_order::UNORDERED:
        return linear_candidates(snapshot, access, query);
    }
    return linear_candidates(snapshot, access, query);
}

bool validate_match_range(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    const data_query_context_t& query,
    std::size_t first,
    std::size_t last_exclusive)
{
    for (std::size_t index = first; index < last_exclusive; ++index) {
        const void* sample = snapshot.at(index);
        if (!sample) {
            return false;
        }
        if (sample_status_for_staging(access, sample, query.nonfinite_policy) !=
            sample_scan_status::ACCEPTED)
        {
            return false;
        }
    }
    return true;
}

sample_scan_status held_sample_status(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    const data_query_context_t& query,
    std::size_t index)
{
    const void* sample = snapshot.at(index);
    if (!sample) {
        return sample_scan_status::FAILED;
    }
    return sample_status_for_staging(access, sample, query.nonfinite_policy);
}

index_window_t validated_time_window(
    const data_snapshot_t& snapshot,
    const Data_access_policy& access,
    const data_query_context_t& query,
    const time_window_candidates_t& candidates)
{
    if (!candidates.valid) {
        return {0, 0, false};
    }

    const bool has_match =
        candidates.match_first < candidates.match_last_exclusive;
    if (has_match && !validate_match_range(
            snapshot,
            access,
            query,
            candidates.match_first,
            candidates.match_last_exclusive))
    {
        return {0, 0, false};
    }

    bool held_accepted = false;
    if (candidates.has_held) {
        const sample_scan_status held_status = held_sample_status(
            snapshot,
            access,
            query,
            candidates.held_index);
        if (held_status == sample_scan_status::FAILED) {
            return {0, 0, false};
        }
        held_accepted = held_status == sample_scan_status::ACCEPTED;
    }

    if (!has_match && !held_accepted) {
        return {0, 0, true};
    }

    if (!has_match) {
        return {
            candidates.held_index,
            candidates.held_index + 1,
            true
        };
    }

    std::size_t first = candidates.match_first;
    std::size_t last = candidates.match_last_exclusive;
    if (held_accepted) {
        first = std::min(first, candidates.held_index);
        last = std::max(last, candidates.held_index + 1);
    }
    return {first, last, true};
}

} // namespace

Time_order Data_source::time_order(std::size_t lod) const
{
    (void)lod;
    return Time_order::UNKNOWN;
}

data_query_result_t<time_range_t> Data_source::time_range(std::size_t lod) const
{
    (void)lod;
    return {};
}

std::vector<std::size_t> Data_source::lod_scales() const
{
    std::vector<std::size_t> scales;
    scales.reserve(lod_levels());
    for (std::size_t level = 0; level < lod_levels(); ++level) {
        scales.push_back(std::max<std::size_t>(1, lod_scale(level)));
    }
    return scales;
}

data_query_result_t<sample_index_window_t> Data_source::query_time_window(
    std::size_t lod,
    const data_query_context_t& query)
{
    data_query_result_t<sample_index_window_t> result;
    if (!query.access || !query.access->get_timestamp) {
        return result;
    }

    const auto snapshot_result = try_snapshot(lod);
    result.sequence = snapshot_result.snapshot.sequence;
    if (snapshot_result.status != snapshot_result_t::Snapshot_status::READY) {
        result.status = status_from_snapshot(snapshot_result.status);
        return result;
    }
    if (!snapshot_result.snapshot) {
        result.status = invalid_ready_snapshot_status(snapshot_result.snapshot);
        return result;
    }
    if (query.time_window.min_ns > query.time_window.max_ns) {
        result.status = Data_query_status::EMPTY;
        return result;
    }

    const time_window_candidates_t candidates = time_window_candidates(
        *this,
        snapshot_result.snapshot,
        *query.access,
        lod,
        query);
    const index_window_t window = validated_time_window(
        snapshot_result.snapshot,
        *query.access,
        query,
        candidates);
    if (!window.valid) {
        result.status = Data_query_status::FAILED;
        return result;
    }
    if (window.first == window.last_exclusive) {
        result.status = Data_query_status::EMPTY;
        return result;
    }

    result.status = Data_query_status::READY;
    result.value = {window.first, window.last_exclusive - window.first};
    return result;
}

data_query_result_t<value_range_t> Data_source::query_v_range(
    std::size_t lod,
    const data_query_context_t& query)
{
    data_query_result_t<value_range_t> result;
    if (!query.access || !query.access->is_valid()) {
        return result;
    }

    const auto snapshot_result = try_snapshot(lod);
    result.sequence = snapshot_result.snapshot.sequence;
    if (snapshot_result.status != snapshot_result_t::Snapshot_status::READY) {
        result.status = status_from_snapshot(snapshot_result.status);
        return result;
    }
    if (!snapshot_result.snapshot) {
        result.status = invalid_ready_snapshot_status(snapshot_result.snapshot);
        return result;
    }
    if (query.time_window.min_ns > query.time_window.max_ns) {
        result.status = Data_query_status::EMPTY;
        return result;
    }

    if (query.profiler) {
        query.profiler->record_counter("renderer.auto_range.range_scan_count");
    }

    value_range_t range;
    bool has_value = false;
    value_range_t held_range;
    bool has_held_candidate = false;
    bool has_held_value = false;
    bool held_candidate_failed = false;
    std::int64_t held_timestamp_ns = 0;
    const bool hold_forward = wants_hold_forward(query);

    for (std::size_t index = 0; index < snapshot_result.snapshot.count; ++index) {
        const void* sample = snapshot_result.snapshot.at(index);
        if (!sample) {
            result.status = Data_query_status::FAILED;
            return result;
        }

        const std::int64_t timestamp_ns = query.access->get_timestamp(sample);
        const bool in_window = time_window_contains(query.time_window, timestamp_ns);
        const bool before_window = timestamp_ns < query.time_window.min_ns;
        if (!in_window && !(hold_forward && before_window)) {
            continue;
        }

        if (hold_forward && before_window &&
            has_held_candidate && timestamp_ns <= held_timestamp_ns)
        {
            continue;
        }

        value_range_t sample_range;
        const sample_scan_status scan_status = sample_value_range(
            *query.access,
            sample,
            query.nonfinite_policy,
            sample_range);

        if (in_window) {
            if (scan_status == sample_scan_status::FAILED) {
                result.status = Data_query_status::FAILED;
                return result;
            }
            if (scan_status == sample_scan_status::ACCEPTED) {
                include_range(range, has_value, sample_range.min, sample_range.max);
            }
            continue;
        }

        if (scan_status == sample_scan_status::ACCEPTED) {
            has_held_candidate = true;
            has_held_value = true;
            held_candidate_failed = false;
            held_timestamp_ns = timestamp_ns;
            held_range = sample_range;
        }
        else
        if (query.nonfinite_policy == Nonfinite_sample_policy::BREAK_SEGMENT) {
            has_held_candidate = true;
            has_held_value = false;
            held_candidate_failed = false;
            held_timestamp_ns = timestamp_ns;
        }
        else
        if (scan_status == sample_scan_status::FAILED) {
            has_held_candidate = true;
            has_held_value = false;
            held_candidate_failed = true;
            held_timestamp_ns = timestamp_ns;
        }
    }

    if (held_candidate_failed) {
        result.status = Data_query_status::FAILED;
        return result;
    }

    if (has_held_value) {
        include_range(range, has_value, held_range.min, held_range.max);
    }

    if (!has_value) {
        result.status = Data_query_status::EMPTY;
        return result;
    }

    result.status = Data_query_status::READY;
    result.value = range;
    return result;
}

} // namespace vnm::plot
