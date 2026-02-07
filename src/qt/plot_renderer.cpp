#include <vnm_plot/qt/plot_renderer.h>
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/qt/vnm_qt_safe_dispatch.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/layout_calculator.h>
#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/range_cache.h>
#include <vnm_plot/core/chrome_renderer.h>
#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/text_renderer.h>

#include <glatter/glatter.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <QDebug>
#include <QOpenGLFramebufferObject>
#include <QMetaObject>
#include <QQuickWindow>
#include <QSize>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vnm::plot {
using namespace detail;

namespace {

bool parse_gl_version_from_string(int& major, int& minor)
{
    const GLubyte* version_str = glGetString(GL_VERSION);
    if (!version_str) {
        return false;
    }

    const auto* version_cstr = reinterpret_cast<const char*>(version_str);
#ifdef _MSC_VER
    return sscanf_s(version_cstr, "%d.%d", &major, &minor) == 2;
#else
    return std::sscanf(version_cstr, "%d.%d", &major, &minor) == 2;
#endif
}

bool get_gl_version(int& major, int& minor)
{
    GLint gl_major = 0;
    GLint gl_minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
    glGetIntegerv(GL_MINOR_VERSION, &gl_minor);

    if (gl_major > 0) {
        major = gl_major;
        minor = gl_minor;
        return true;
    }

    return parse_gl_version_from_string(major, minor);
}

bool has_required_opengl()
{
    int major = 0;
    int minor = 0;
    if (!get_gl_version(major, minor)) {
        qWarning() << "vnm_plot: unable to query OpenGL version.";
        return false;
    }

    const bool version_ok = (major > 4) || (major == 4 && minor >= 3);
    if (!version_ok) {
        qWarning() << "vnm_plot: OpenGL 4.3+ is required, detected" << major << "." << minor;
        return false;
    }

    if (!glatter_GL_ARB_gpu_shader_int64) {
        qWarning() << "vnm_plot: GL_ARB_gpu_shader_int64 is required.";
        return false;
    }

    return true;
}

bool spans_approx_equal(double a, double b)
{
    const double abs_a = std::abs(a);
    const double abs_b = std::abs(b);
    const double diff = std::abs(a - b);
    const double scale = std::max(1.0, std::max(abs_a, abs_b));
    return diff <= scale * k_eps;
}

std::uint64_t hash_data_sources(const std::map<int, std::shared_ptr<const series_data_t>>& series_map)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& [id, series] : series_map) {
        if (!series) {
            continue;
        }

        const Data_source* main_source = series->main_source();
        const Data_source* preview_source = series->preview_source();
        const bool has_preview = series->has_preview_config();

        const std::uint64_t main_ptr = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(main_source));
        const std::uint64_t preview_ptr = has_preview
            ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(preview_source))
            : 0ULL;
        const std::uint64_t preview_layout_key = has_preview
            ? series->preview_access().layout_key
            : 0ULL;
        const Display_style preview_style = series->effective_preview_style();
        const std::uint64_t preview_style_bits =
            (has_preview && preview_style != series->style)
                ? static_cast<std::uint64_t>(preview_style)
                : 0ULL;

        std::uint64_t value = static_cast<std::uint64_t>(id);
        value ^= main_ptr + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
        if (has_preview) {
            value ^= 0x0f0f0f0f0f0f0f0fULL + (value << 6) + (value >> 2);
            value ^= preview_ptr + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
            value ^= preview_layout_key + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
            value ^= preview_style_bits + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
        }
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    }
    return hash;
}

std::uint64_t hash_series_snapshot(const std::map<int, std::shared_ptr<const series_data_t>>& series_map)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& [id, series] : series_map) {
        if (!series) {
            continue;
        }
        const std::uint64_t series_ptr = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(series.get()));
        std::uint64_t value = static_cast<std::uint64_t>(id);
        value ^= series_ptr + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    }
    return hash;
}

constexpr float k_auto_v_padding_factor = 0.125f;
constexpr float k_auto_v_padding_min = 0.5f;
constexpr float k_auto_v_sync_eps = static_cast<float>(k_eps);
constexpr float k_anim_span_min = static_cast<float>(k_eps);
constexpr float k_anim_target_frac = 0.001f;

bool scan_snapshot_minmax(
    const Data_access_policy& access,
    const data_snapshot_t& snapshot,
    std::size_t start_idx,
    std::size_t end_idx,
    float& out_min,
    float& out_max)
{
    if (!access.get_value && !access.get_range) {
        return false;
    }

    if (!snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        return false;
    }

    if (start_idx >= snapshot.count || end_idx <= start_idx) {
        return false;
    }

    end_idx = std::min(end_idx, snapshot.count);

    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    for (std::size_t i = start_idx; i < end_idx; ++i) {
        const void* sample = snapshot.at(i);
        if (!sample) {
            continue;
        }

        float low = 0.0f;
        float high = 0.0f;
        if (access.get_range) {
            auto [lo, hi] = access.get_range(sample);
            low = lo;
            high = hi;
        }
        else if (access.get_value) {
            low = access.get_value(sample);
            high = low;
        }
        else {
            continue;
        }

        if (!std::isfinite(low) || !std::isfinite(high)) {
            continue;
        }

        v_min = std::min(v_min, std::min(low, high));
        v_max = std::max(v_max, std::max(low, high));
        have_any = true;
    }

    if (!have_any) {
        return false;
    }

    out_min = v_min;
    out_max = v_max;
    return true;
}

bool find_window_indices(
    const Data_access_policy& access,
    const data_snapshot_t& snapshot,
    double t_min,
    double t_max,
    std::size_t& start_idx,
    std::size_t& end_idx)
{
    start_idx = 0;
    end_idx = snapshot.count;

    if (!access.get_timestamp || !snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        return false;
    }
    if (!(t_max > t_min)) {
        return true;
    }

    const void* first_sample = snapshot.at(0);
    const void* last_sample = snapshot.at(snapshot.count - 1);
    if (!first_sample || !last_sample) {
        return false;
    }
    const double first_ts = access.get_timestamp(first_sample);
    const double last_ts = access.get_timestamp(last_sample);
    if (std::isfinite(first_ts) && std::isfinite(last_ts) &&
        (t_max < first_ts || t_min > last_ts)) {
        return false;
    }

    const auto& get_ts = access.get_timestamp;
    std::size_t lb = detail::lower_bound_timestamp(snapshot, get_ts, t_min);
    start_idx = (lb > 0) ? (lb - 1) : 0;
    std::size_t ub = detail::upper_bound_timestamp(snapshot, get_ts, t_max);
    end_idx = std::min(ub + 1, snapshot.count);
    if (end_idx <= start_idx) {
        end_idx = std::min(start_idx + 1, snapshot.count);
    }
    return true;
}

bool get_lod_minmax(
    Data_source& data_source,
    const Data_access_policy& access,
    series_minmax_cache_t& cache,
    std::size_t level,
    float& out_min,
    float& out_max,
    vnm::plot::Profiler* profiler)
{
    if (level >= cache.lods.size()) {
        return false;
    }

    auto& entry = cache.lods[level];

    data_snapshot_t snapshot;
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.range_calc.get_lod_minmax.snapshot");
        snapshot = data_source.snapshot(level);
    }
    if (!snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        if (entry.valid) {
            out_min = entry.v_min;
            out_max = entry.v_max;
            return true;
        }
        return false;
    }
    if (entry.valid && entry.sequence == snapshot.sequence) {
        out_min = entry.v_min;
        out_max = entry.v_max;
        return true;
    }

    float v_min = 0.0f;
    float v_max = 0.0f;
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.range_calc.get_lod_minmax.scan");
        if (!scan_snapshot_minmax(access, snapshot, 0, snapshot.count, v_min, v_max)) {
            return false;
        }
    }

    entry.v_min = v_min;
    entry.v_max = v_max;
    entry.sequence = snapshot.sequence;
    entry.valid = true;

    out_min = v_min;
    out_max = v_max;
    return true;
}

struct series_view_t
{
    Data_source* source = nullptr;
    const Data_access_policy* access = nullptr;
    std::unordered_map<int, series_minmax_cache_t>* cache = nullptr;
};

void purge_stale_cache(std::unordered_map<int, series_minmax_cache_t>& cache,
                       const std::unordered_set<int>& active)
{
    for (auto it = cache.begin(); it != cache.end();) {
        if (active.find(it->first) == active.end()) it = cache.erase(it);
        else ++it;
    }
}

template <typename Resolver>
std::pair<float, float> compute_global_v_range(
    const std::map<int, std::shared_ptr<const series_data_t>>& series_map,
    std::unordered_map<int, series_minmax_cache_t>& cache_map,
    std::unordered_map<int, series_minmax_cache_t>* alt_cache_map,
    Resolver&& resolve,
    vnm::plot::Profiler* profiler,
    bool use_lod_cache,
    float fallback_min,
    float fallback_max)
{
    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.range_calc.global");

    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    std::unordered_set<int> active_ids;
    std::unordered_set<int> active_alt_ids;

    for (const auto& [id, series] : series_map) {
        if (!series || !series->enabled) {
            continue;
        }

        series_view_t view;
        if (!resolve(id, *series, view)) {
            continue;
        }
        if (!view.source || !view.access || !view.cache) {
            continue;
        }
        if (!view.access->get_value && !view.access->get_range) {
            continue;
        }

        if (view.cache == &cache_map) {
            active_ids.insert(id);
        }
        else if (alt_cache_map && view.cache == alt_cache_map) {
            active_alt_ids.insert(id);
        }

        float series_min = 0.0f;
        float series_max = 0.0f;
        bool got_range = false;

        if (view.source->has_value_range() && !view.source->value_range_needs_rescan()) {
            auto [ds_min, ds_max] = view.source->value_range();
            if (std::isfinite(ds_min) && std::isfinite(ds_max) && ds_min <= ds_max) {
                series_min = ds_min;
                series_max = ds_max;
                got_range = true;
            }
        }

        if (!got_range) {
            // Slow path: scan LOD data with caching.
            series_minmax_cache_t& cache = (*view.cache)[id];
            const void* identity = view.source->identity();
            const std::size_t levels = view.source->lod_levels();
            if (levels == 0) {
                continue;
            }

            if (cache.identity != identity || cache.lods.size() != levels) {
                cache.identity = identity;
                cache.lods.assign(levels, lod_minmax_cache_t{});
            }

            const std::size_t level = use_lod_cache ? (levels - 1) : 0;
            if (!get_lod_minmax(*view.source, *view.access, cache, level, series_min, series_max, profiler)) {
                if (level == 0 ||
                    !get_lod_minmax(*view.source, *view.access, cache, 0, series_min, series_max, profiler)) {
                    continue;
                }
            }
        }

        v_min = std::min(v_min, series_min);
        v_max = std::max(v_max, series_max);
        have_any = true;
    }

    purge_stale_cache(cache_map, active_ids);
    if (alt_cache_map) purge_stale_cache(*alt_cache_map, active_alt_ids);

    if (!have_any) {
        return {fallback_min, fallback_max};
    }

    return {v_min, v_max};
}

template <typename Resolver>
std::pair<float, float> compute_visible_v_range(
    const std::map<int, std::shared_ptr<const series_data_t>>& series_map,
    std::unordered_map<int, series_minmax_cache_t>& cache_map,
    std::unordered_map<int, series_minmax_cache_t>* alt_cache_map,
    Resolver&& resolve,
    vnm::plot::Profiler* profiler,
    double t_min,
    double t_max,
    double width_px,
    float fallback_min,
    float fallback_max)
{
    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.range_calc.visible");

    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    std::unordered_set<int> active_ids;
    std::unordered_set<int> active_alt_ids;

    for (const auto& [id, series] : series_map) {
        if (!series || !series->enabled) {
            continue;
        }

        series_view_t view;
        if (!resolve(id, *series, view)) {
            continue;
        }
        if (!view.source || !view.access || !view.cache) {
            continue;
        }

        if (view.cache == &cache_map) {
            active_ids.insert(id);
        }
        else if (alt_cache_map && view.cache == alt_cache_map) {
            active_alt_ids.insert(id);
        }

        float series_min = 0.0f;
        float series_max = 0.0f;
        uint64_t query_sequence = 0;
        if (view.source->query_v_range_for_t_window(
                t_min, t_max, series_min, series_max, &query_sequence))
        {
            if (!std::isfinite(series_min) || !std::isfinite(series_max) || series_min > series_max) {
                continue;
            }
            series_minmax_cache_t& cache = (*view.cache)[id];
            const void* identity = view.source->identity();
            const std::size_t levels = view.source->lod_levels();
            if (cache.identity != identity || cache.lods.size() != levels) {
                cache.identity = identity;
                cache.lods.assign(levels, lod_minmax_cache_t{});
            }
            cache.query_sequence = query_sequence;
            cache.query_sequence_valid = (query_sequence != 0);
            v_min = std::min(v_min, series_min);
            v_max = std::max(v_max, series_max);
            have_any = true;
            continue;
        }

        if (!view.access->get_timestamp) continue;
        if (!view.access->get_value && !view.access->get_range) continue;

        const std::size_t levels = view.source->lod_levels();
        if (levels == 0) continue;

        data_snapshot_t snapshot0 = view.source->snapshot(0);
        if (!snapshot0 || snapshot0.count == 0 || snapshot0.stride == 0) continue;

        std::size_t start0 = 0, end0 = 0;
        if (!find_window_indices(*view.access, snapshot0, t_min, t_max, start0, end0)) continue;
        const std::size_t count0 = (end0 > start0) ? (end0 - start0) : 0;
        if (count0 == 0) continue;

        auto scales = compute_lod_scales(*view.source);
        const std::size_t base_scale = scales.empty() ? 1 : scales[0];
        const std::size_t base_samples = count0 * base_scale;
        const double base_pps = (base_samples > 0 && width_px > 0.0)
            ? width_px / static_cast<double>(base_samples)
            : 0.0;
        const std::size_t desired_level = choose_lod_level(scales, 0, base_pps);

        std::size_t applied_level = desired_level;
        data_snapshot_t snapshot = view.source->snapshot(applied_level);
        if (!snapshot || snapshot.count == 0 || snapshot.stride == 0) {
            snapshot = snapshot0;
            applied_level = 0;
        }

        std::size_t start = 0, end = 0;
        if (!find_window_indices(*view.access, snapshot, t_min, t_max, start, end)) continue;

        const bool full_range = (start == 0 && end == snapshot.count);

        series_minmax_cache_t& cache = (*view.cache)[id];
        const void* identity = view.source->identity();
        if (cache.identity != identity || cache.lods.size() != levels) {
            cache.identity = identity;
            cache.lods.assign(levels, lod_minmax_cache_t{});
        }
        cache.query_sequence = 0;
        cache.query_sequence_valid = false;

        if (full_range) {
            if (!get_lod_minmax(*view.source, *view.access, cache, applied_level, series_min, series_max, profiler)) {
                continue;
            }
        }
        else {
            if (!scan_snapshot_minmax(*view.access, snapshot, start, end,
                    series_min, series_max)) {
                continue;
            }
        }

        v_min = std::min(v_min, series_min);
        v_max = std::max(v_max, series_max);
        have_any = true;
    }

    purge_stale_cache(cache_map, active_ids);
    if (alt_cache_map) purge_stale_cache(*alt_cache_map, active_alt_ids);

    if (!have_any) {
        return {fallback_min, fallback_max};
    }

    const float span = v_max - v_min;
    const float padding = (span > 0.0f) ? span * k_auto_v_padding_factor : k_auto_v_padding_min;
    return {v_min - padding, v_max + padding};
}

} // namespace

struct Plot_renderer::impl_t
{
    struct range_cache_key_t
    {
        Auto_v_range_mode auto_mode = Auto_v_range_mode::GLOBAL;
        bool v_auto = true;
        bool preview_enabled = false;
        double extra_scale = 0.0;
        double t_min = 0.0;
        double t_max = 0.0;
        double usable_width = 0.0;

        bool operator==(const range_cache_key_t& other) const noexcept
        {
            return auto_mode == other.auto_mode &&
                   v_auto == other.v_auto &&
                   preview_enabled == other.preview_enabled &&
                   extra_scale == other.extra_scale &&
                   t_min == other.t_min &&
                   t_max == other.t_max &&
                   usable_width == other.usable_width;
        }

        bool operator!=(const range_cache_key_t& other) const noexcept
        {
            return !(*this == other);
        }
    };

    struct render_snapshot_t
    {
        data_config_t cfg;
        bool visible = false;
        bool show_info = false;
        bool v_auto = true;
        bool reset_view_state = false;

        Plot_config config;

        std::uint64_t data_signature = 0;
        std::uint64_t series_snapshot_signature = 0;

        double adjusted_font_px = 12.0;
        double base_label_height_px = 14.0;
        double vbar_width_pixels = 0.0;
        double adjusted_reserved_height = 0.0;
        double adjusted_preview_height = 0.0;
    };

    struct view_state_t
    {
        // Range throttling state (Phase 1 optimization).
        // Reduces range_calc from render rate (~19 Hz) to throttled rate (~4 Hz).
        // NOTE: Data sequence changes within the throttle interval may cause up to
        // 250ms staleness. The internal per-LOD cache in compute_*_v_range handles
        // sequence-based invalidation, so this is mainly a loop/call overhead skip.
        std::chrono::steady_clock::time_point last_range_calc_time{};
        float cached_v0 = 0.0f;
        float cached_v1 = 0.0f;
        float cached_preview_v0 = 0.0f;
        float cached_preview_v1 = 0.0f;
        bool range_cache_valid = false;
        range_cache_key_t last_range_key{};
        static constexpr std::chrono::milliseconds k_range_throttle_interval{250};

        // V-range animation state.
        float anim_v0 = 0.0f;
        float anim_v1 = 1.0f;
        float anim_preview_v0 = 0.0f;
        float anim_preview_v1 = 1.0f;
        std::chrono::steady_clock::time_point last_anim_time{};
        bool anim_initialized = false;
        static constexpr float k_v_anim_speed = 15.0f;  // ~200ms to 95%

        double last_vertical_span = 0.0;
        double last_vertical_seed_step = 0.0;
        double last_horizontal_span = 0.0;
        double last_horizontal_seed_step = 0.0;
        int last_vertical_seed_index = -1;
        int last_horizontal_seed_index = -1;

        std::map<int, std::shared_ptr<const series_data_t>> series_snapshot;
        std::unordered_map<int, series_minmax_cache_t> v_range_cache;
        std::unordered_map<int, series_minmax_cache_t> preview_v_range_cache;
        Layout_cache layout_cache;

        std::uint64_t data_signature = 0;
        std::uint64_t series_snapshot_signature = 0;

        void reset_for_data_change()
        {
            range_cache_valid = false;
            last_range_calc_time = {};
            cached_v0 = 0.0f;
            cached_v1 = 0.0f;
            cached_preview_v0 = 0.0f;
            cached_preview_v1 = 0.0f;
            last_range_key = {};

            anim_initialized = false;
            last_anim_time = {};
            anim_v0 = 0.0f;
            anim_v1 = 1.0f;
            anim_preview_v0 = 0.0f;
            anim_preview_v1 = 1.0f;

            last_vertical_span = 0.0;
            last_vertical_seed_step = 0.0;
            last_horizontal_span = 0.0;
            last_horizontal_seed_step = 0.0;
            last_vertical_seed_index = -1;
            last_horizontal_seed_index = -1;

            v_range_cache.clear();
            preview_v_range_cache.clear();
            layout_cache.invalidate();
        }
    };

    // Field order optimized to minimize padding (clang-tidy warning).
    Asset_loader asset_loader;
    Primitive_renderer primitives;
    render_snapshot_t snapshot;
    const Plot_widget* owner = nullptr;
    Font_renderer fonts;
    std::unique_ptr<Text_renderer> text;

    Layout_calculator layout_calc;
    Series_renderer series;
    view_state_t view;

    int init_failed_status = 0;
    int last_font_px = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    int last_opengl_status = std::numeric_limits<int>::min();
    int last_hlabels_subsecond = -1;
    std::uint32_t assets_revision = 0;

    Chrome_renderer chrome;
    bool assets_initialized = false;
    bool assets_registered = false;
    bool initialized = false;
    bool init_failed = false;
    bool methods_checked = false;
    bool has_opengl_status_method = false;
    bool has_hlabels_subsecond_method = false;

    const frame_layout_result_t& calculate_frame_layout(
        float v0,
        float v1,
        double t0,
        double t1,
        int win_w,
        int win_h);

    void update_seed_history(
        double v_span,
        double t_span,
        const frame_layout_result_t& layout);
};

const frame_layout_result_t& Plot_renderer::impl_t::calculate_frame_layout(
    float v0,
    float v1,
    double t0,
    double t1,
    int win_w,
    int win_h)
{
    const double usable_height = win_h - snapshot.adjusted_reserved_height;
    const double v_span = double(v1) - double(v0);
    const double t_span = t1 - t0;
    const Plot_config* config = &snapshot.config;
    vnm::plot::Profiler* profiler = config ? config->profiler.get() : nullptr;

    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.calculate_layout.impl");

    Layout_cache::key_t cache_key;
    cache_key.v0 = v0;
    cache_key.v1 = v1;
    cache_key.t0 = t0;
    cache_key.t1 = t1;
    cache_key.viewport_size = Size2i{win_w, win_h};
    cache_key.adjusted_reserved_height = snapshot.adjusted_reserved_height;
    cache_key.adjusted_preview_height = snapshot.adjusted_preview_height;
    cache_key.adjusted_font_size_in_pixels = snapshot.adjusted_font_px;
    cache_key.vbar_width_pixels = snapshot.vbar_width_pixels;
    cache_key.font_metrics_key = fonts.text_measure_cache_key();

    if (const auto* cached = view.layout_cache.try_get(cache_key)) {
        VNM_PLOT_PROFILE_SCOPE(
            profiler,
            "renderer.frame.calculate_layout.impl.cache_hit");
        return *cached;
    }

    VNM_PLOT_PROFILE_SCOPE(
        profiler,
        "renderer.frame.calculate_layout.impl.cache_miss");

    auto build_layout_params = [&](double vbar_width) {
        Layout_calculator::parameters_t layout_params;

        layout_params.v_min = v0;
        layout_params.v_max = v1;
        layout_params.t_min = t0;
        layout_params.t_max = t1;
        layout_params.usable_width = win_w - vbar_width;
        layout_params.usable_height = usable_height;
        layout_params.vbar_width = vbar_width;
        layout_params.label_visible_height = usable_height + snapshot.adjusted_preview_height;
        layout_params.adjusted_font_size_in_pixels = snapshot.adjusted_font_px;
        layout_params.monospace_char_advance_px = fonts.monospace_advance_px();
        layout_params.monospace_advance_is_reliable = fonts.monospace_advance_is_reliable();
        layout_params.measure_text_cache_key = fonts.text_measure_cache_key();
        layout_params.measure_text_func = [this](const char* text) {
            return fonts.measure_text_px(text);
        };
        layout_params.h_label_vertical_nudge_factor = k_h_label_vertical_nudge_px;

        if (v_span > 0.0 &&
            view.last_vertical_seed_index >= 0 &&
            spans_approx_equal(v_span, view.last_vertical_span) &&
            view.last_vertical_seed_step > 0.0)
        {
            layout_params.has_vertical_seed = true;
            layout_params.vertical_seed_index = view.last_vertical_seed_index;
            layout_params.vertical_seed_step = view.last_vertical_seed_step;
        }

        if (t_span > 0.0 &&
            view.last_horizontal_seed_index >= 0 &&
            spans_approx_equal(t_span, view.last_horizontal_span) &&
            view.last_horizontal_seed_step > 0.0)
        {
            layout_params.has_horizontal_seed = true;
            layout_params.horizontal_seed_index = view.last_horizontal_seed_index;
            layout_params.horizontal_seed_step = view.last_horizontal_seed_step;
        }

        if (config) {
            layout_params.get_required_fixed_digits_func = [](double) { return 2; };
            layout_params.format_timestamp_func = [config](double ts, double range) -> std::string {
                if (config->format_timestamp) {
                    return config->format_timestamp(ts, range);
                }
                return format_axis_fixed_or_int(ts, 3);
            };
            layout_params.profiler = config->profiler.get();
        }

        return layout_params;
    };

    auto layout_params = build_layout_params(snapshot.vbar_width_pixels);
    auto layout_result = layout_calc.calculate(layout_params);

    double measured_vbar_width = std::max(
        k_vbar_min_width_px_d,
        double(layout_result.max_v_label_text_width) + k_v_label_horizontal_padding_px);
    if (!std::isfinite(measured_vbar_width) || measured_vbar_width <= 0.0) {
        measured_vbar_width = k_vbar_min_width_px_d;
    }

    // If measured width differs significantly, notify widget to animate towards it.
    // Continue using the current animated width for this frame - the animation will
    // progress smoothly on subsequent frames via the widget's timer.
    double effective_vbar_width = snapshot.vbar_width_pixels;
    if (!std::isfinite(effective_vbar_width) || effective_vbar_width <= 0.0) {
        effective_vbar_width = measured_vbar_width;
    }
    if (std::abs(snapshot.vbar_width_pixels - measured_vbar_width) > k_vbar_width_change_threshold_d)
    {
        // Notify widget to animate to new width
        if (owner) {
            vnm::post_invoke(
                const_cast<Plot_widget*>(owner),
                &Plot_widget::set_vbar_width_from_renderer,
                measured_vbar_width);
        }

        // For this frame, continue using the current animated width,
        // which will converge toward measured_vbar_width via the widget's timer.
        // Recalculate layout with current animated width to keep labels consistent.
        {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.calculate_layout.impl.cache_miss.pass2");
            layout_params = build_layout_params(effective_vbar_width);
            layout_result = layout_calc.calculate(layout_params);
        }
    }

    {
        VNM_PLOT_PROFILE_SCOPE(
            profiler,
            "renderer.frame.calculate_layout.impl.cache_miss.assemble");

        frame_layout_result_t frame_layout;
        frame_layout.usable_width = win_w - effective_vbar_width;
        frame_layout.usable_height = usable_height;
        frame_layout.v_bar_width = effective_vbar_width;
        frame_layout.h_bar_height = snapshot.base_label_height_px + k_scissor_pad_px;
        frame_layout.max_v_label_text_width = layout_result.max_v_label_text_width;
        frame_layout.v_labels = std::move(layout_result.v_labels);
        frame_layout.h_labels = std::move(layout_result.h_labels);
        frame_layout.v_label_fixed_digits = layout_result.v_label_fixed_digits;
        frame_layout.h_labels_subsecond = layout_result.h_labels_subsecond;
        frame_layout.vertical_seed_index = layout_result.vertical_seed_index;
        frame_layout.vertical_seed_step = layout_result.vertical_seed_step;
        frame_layout.vertical_finest_step = layout_result.vertical_finest_step;
        frame_layout.horizontal_seed_index = layout_result.horizontal_seed_index;
        frame_layout.horizontal_seed_step = layout_result.horizontal_seed_step;

        VNM_PLOT_PROFILE_SCOPE(
            profiler,
            "renderer.frame.calculate_layout.impl.cache_miss.finalize");
        return view.layout_cache.store(cache_key, std::move(frame_layout));
    }
}

void Plot_renderer::impl_t::update_seed_history(
    double v_span,
    double t_span,
    const frame_layout_result_t& layout)
{
    if (v_span > 0.0) {
        view.last_vertical_span = v_span;
        view.last_vertical_seed_index = layout.vertical_seed_index;
        view.last_vertical_seed_step = layout.vertical_seed_step;
    }
    else {
        view.last_vertical_span = 0.0;
        view.last_vertical_seed_index = -1;
        view.last_vertical_seed_step = 0.0;
    }

    if (t_span > 0.0) {
        view.last_horizontal_span = t_span;
        view.last_horizontal_seed_index = layout.horizontal_seed_index;
        view.last_horizontal_seed_step = layout.horizontal_seed_step;
    }
    else {
        view.last_horizontal_span = 0.0;
        view.last_horizontal_seed_index = -1;
        view.last_horizontal_seed_step = 0.0;
    }
}

Plot_renderer::Plot_renderer(const Plot_widget* owner)
    : m_impl(std::make_unique<impl_t>())
{
    m_impl->owner = owner;
}

Plot_renderer::~Plot_renderer()
{
    if (m_impl->initialized) {
        m_impl->primitives.cleanup_gl_resources();
        m_impl->series.cleanup_gl_resources();
        Font_renderer::cleanup_thread_resources();
    }
}

void Plot_renderer::synchronize(QQuickFramebufferObject* fbo_item)
{
    auto* widget = static_cast<Plot_widget*>(fbo_item);
    if (!widget) {
        return;
    }

    if (!m_impl->methods_checked) {
        const QMetaObject* meta = widget->metaObject();
        m_impl->has_opengl_status_method =
            meta && meta->indexOfMethod("set_opengl_status_from_renderer(int)") >= 0;
        m_impl->has_hlabels_subsecond_method =
            meta && meta->indexOfMethod("set_hlabels_subsecond_from_renderer(bool)") >= 0;
        m_impl->methods_checked = true;
    }

    // Copy configuration
    {
        std::shared_lock lock(widget->m_config_mutex);
        m_impl->snapshot.config = widget->m_config;
    }

    // Copy data config
    {
        std::shared_lock lock(widget->m_data_cfg_mutex);
        m_impl->snapshot.cfg = widget->m_data_cfg;
    }
    m_impl->snapshot.vbar_width_pixels = widget->vbar_width_pixels();

    // Snapshot series signature (main + preview sources/styles/layouts)
    {
        std::shared_lock lock(widget->m_series_mutex);
        m_impl->snapshot.data_signature = hash_data_sources(widget->m_series);
        m_impl->snapshot.series_snapshot_signature = hash_series_snapshot(widget->m_series);
    }

    // Copy UI state
    m_impl->snapshot.visible = (widget->width() > 0.0 && widget->height() > 0.0);
    m_impl->snapshot.show_info = widget->m_show_info.load(std::memory_order_acquire);
    m_impl->snapshot.v_auto = widget->m_v_auto.load(std::memory_order_acquire);
    m_impl->snapshot.reset_view_state = widget->consume_view_state_reset_request();
    m_impl->snapshot.adjusted_font_px = widget->m_adjusted_font_size;
    m_impl->snapshot.base_label_height_px = widget->m_base_label_height;
    m_impl->snapshot.adjusted_preview_height = widget->m_adjusted_preview_height;
    m_impl->snapshot.adjusted_reserved_height = widget->m_base_label_height + widget->m_adjusted_preview_height;
}

void Plot_renderer::render()
{
    auto notify_opengl_status = [&](int status) {
        if (!m_impl->owner || !m_impl->has_opengl_status_method) {
            return;
        }
        if (m_impl->last_opengl_status == status) {
            return;
        }
        m_impl->last_opengl_status = status;
        QMetaObject::invokeMethod(
            const_cast<Plot_widget*>(m_impl->owner),
            "set_opengl_status_from_renderer",
            Qt::QueuedConnection,
            Q_ARG(int, status));
    };

    auto notify_hlabels_subsecond = [&](bool subsecond) {
        if (!m_impl->owner || !m_impl->has_hlabels_subsecond_method) {
            return;
        }
        const int state = subsecond ? 1 : 0;
        if (m_impl->last_hlabels_subsecond == state) {
            return;
        }
        m_impl->last_hlabels_subsecond = state;
        QMetaObject::invokeMethod(
            const_cast<Plot_widget*>(m_impl->owner),
            "set_hlabels_subsecond_from_renderer",
            Qt::QueuedConnection,
            Q_ARG(bool, subsecond));
    };

    const Plot_config* config = &m_impl->snapshot.config;
    vnm::plot::Profiler* profiler = (config && config->profiler)
        ? config->profiler.get()
        : nullptr;

    if (!m_impl->snapshot.visible) {
        return;
    }

    if (m_impl->init_failed) {
        if (m_impl->init_failed_status != 0) {
            notify_opengl_status(m_impl->init_failed_status);
        }
        return;
    }

    auto register_assets_if_needed = [&]() {
        if (!config || !config->register_assets) {
            return;
        }
        const std::uint32_t desired_revision = config->assets_revision;
        if (m_impl->assets_registered && m_impl->assets_revision == desired_revision) {
            return;
        }
        config->register_assets(m_impl->asset_loader);
        m_impl->assets_registered = true;
        m_impl->assets_revision = desired_revision;
    };

    // Initialize GL resources on first render
    if (!m_impl->initialized) {
        glatter_get_extension_support_GL();

        if (!has_required_opengl()) {
            m_impl->init_failed = true;
            m_impl->initialized = true;
            m_impl->init_failed_status = -2;
            notify_opengl_status(-2);
            return;
        }

        if (!m_impl->assets_initialized) {
            init_embedded_assets(m_impl->asset_loader);
            m_impl->assets_initialized = true;
        }
        register_assets_if_needed();

        if (!m_impl->primitives.initialize(m_impl->asset_loader)) {
            notify_opengl_status(-3);
            return;
        }
        m_impl->series.initialize(m_impl->asset_loader);

        const int font_px = static_cast<int>(std::round(m_impl->snapshot.adjusted_font_px));
        m_impl->fonts.initialize(m_impl->asset_loader, font_px);
        m_impl->last_font_px = font_px;

        m_impl->text = std::make_unique<Text_renderer>(&m_impl->fonts);

        m_impl->initialized = true;
        notify_opengl_status(1);
    }

    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer");
    VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame");

    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.config_sync");

        if (m_impl->assets_initialized) {
            register_assets_if_needed();
        }

        const int desired_font_px = static_cast<int>(std::round(m_impl->snapshot.adjusted_font_px));
        if (desired_font_px > 0 && desired_font_px != m_impl->last_font_px) {
            m_impl->fonts.initialize(m_impl->asset_loader, desired_font_px, true);
            m_impl->last_font_px = desired_font_px;
        }

        // Get configuration
        m_impl->asset_loader.set_log_callback(config ? config->log_error : nullptr);
        m_impl->primitives.set_log_callback(config ? config->log_error : nullptr);
        m_impl->primitives.set_profiler(profiler);
        m_impl->fonts.set_log_callbacks(
            config ? config->log_error : nullptr,
            config ? config->log_debug : nullptr);
    }

    // Get viewport size
    const int win_w = m_impl->viewport_width;
    const int win_h = m_impl->viewport_height;

    if (win_w <= 0 || win_h <= 0) {
        return;
    }

    const double preview_visibility = config ? config->preview_visibility : 1.0;
    const bool preview_enabled =
        (m_impl->snapshot.adjusted_preview_height > 0.0) && (preview_visibility > 0.0);

    const std::uint64_t data_signature = m_impl->snapshot.data_signature;
    const std::uint64_t series_signature = m_impl->snapshot.series_snapshot_signature;
    const bool data_changed = (data_signature != m_impl->view.data_signature);
    const bool view_reset = m_impl->snapshot.reset_view_state;
    if (data_changed || view_reset) {
        // Avoid smoothing when the series set changes or the UI requests a reset.
        m_impl->view.data_signature = data_signature;
        m_impl->view.reset_for_data_change();
    }

    const bool series_snapshot_changed =
        (series_signature != m_impl->view.series_snapshot_signature);
    if (m_impl->owner &&
        (data_changed || series_snapshot_changed || m_impl->view.series_snapshot.empty()))
    {
        std::shared_lock lock(m_impl->owner->m_series_mutex);
        m_impl->view.series_snapshot = m_impl->owner->m_series;
        m_impl->view.series_snapshot_signature = series_signature;
    }
    const auto& series_snapshot = m_impl->view.series_snapshot;

    const bool prev_v_auto = (data_changed || view_reset)
        ? m_impl->snapshot.v_auto
        : m_impl->view.last_range_key.v_auto;
    float v0 = 0.0f;
    float v1 = 0.0f;
    float preview_v0 = 0.0f;
    float preview_v1 = 0.0f;
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.range_calc");

        const auto now = std::chrono::steady_clock::now();
        const bool throttle_expired =
            (now - m_impl->view.last_range_calc_time) >= impl_t::view_state_t::k_range_throttle_interval;

        // Calculate layout
        const double usable_width = win_w - m_impl->snapshot.vbar_width_pixels;

        // Determine v-range mode and config.
        const Auto_v_range_mode auto_mode =
            config ? config->auto_v_range_mode : Auto_v_range_mode::GLOBAL;
        const double auto_v_extra_scale = config ? config->auto_v_range_extra_scale : 0.0;
        const bool v_auto = m_impl->snapshot.v_auto;
        const bool can_use_series = (m_impl->owner != nullptr) && !series_snapshot.empty();

        // Check for cache invalidation conditions.
        // Invalidate when: mode changes, v_auto changes, config changes, or window changes.
        impl_t::range_cache_key_t current_key;
        current_key.auto_mode = auto_mode;
        current_key.v_auto = v_auto;
        current_key.preview_enabled = preview_enabled;
        current_key.extra_scale = auto_v_extra_scale;
        if (auto_mode == Auto_v_range_mode::VISIBLE) {
            current_key.t_min = m_impl->snapshot.cfg.t_min;
            current_key.t_max = m_impl->snapshot.cfg.t_max;
            current_key.usable_width = usable_width;
        }

        bool cache_invalid = !m_impl->view.range_cache_valid ||
                             (current_key != m_impl->view.last_range_key);

        if (!cache_invalid && !throttle_expired && v_auto && can_use_series) {
            cache_invalid = !validate_range_cache_sequences(
                series_snapshot,
                m_impl->view.v_range_cache,
                auto_mode);
            if (!cache_invalid && preview_enabled) {
                cache_invalid = !validate_preview_range_cache_sequences(
                    series_snapshot,
                    m_impl->view.preview_v_range_cache,
                    auto_mode);
            }
        }

        if (!cache_invalid && !throttle_expired) {
            // Use cached values (throttled path).
            if (v_auto) {
                v0 = m_impl->view.cached_v0;
                v1 = m_impl->view.cached_v1;
            }
            else {
                // When v_auto is false, always use manual config directly.
                v0 = m_impl->snapshot.cfg.v_manual_min;
                v1 = m_impl->snapshot.cfg.v_manual_max;
            }
            if (preview_enabled) {
                preview_v0 = m_impl->view.cached_preview_v0;
                preview_v1 = m_impl->view.cached_preview_v1;
            }
            else {
                preview_v0 = v0;
                preview_v1 = v1;
            }
        }
        else {
            // Perform full range calculation.
            const auto apply_auto_padding = [&](float& v_min, float& v_max) {
                if (!(auto_v_extra_scale > 0.0)) {
                    return;
                }

                const double span = double(v_max) - double(v_min);
                if (!(span > 0.0)) {
                    return;
                }

                const double center = 0.5 * (double(v_min) + double(v_max));
                const double padded_span = span * (1.0 + auto_v_extra_scale);
                v_min = static_cast<float>(center - padded_span * 0.5);
                v_max = static_cast<float>(center + padded_span * 0.5);
            };

        if (can_use_series) {
            const auto resolve_main = [&](int /*series_id*/,
                                          const series_data_t& series,
                                          series_view_t& view) -> bool {
                    Data_source* source = series.main_source();
                    if (!source) {
                        return false;
                    }
                    view.source = source;
                    view.access = &series.main_access();
                    view.cache = &m_impl->view.v_range_cache;
                    return true;
                };

                const auto resolve_preview = [&](int /*series_id*/,
                                                 const series_data_t& series,
                                                 series_view_t& view) -> bool {
                    Data_source* source = series.preview_source();
                    if (!source) {
                        return false;
                    }
                    if (series.preview_access_invalid_for_source()) {
                        return false;
                    }
                    const Data_access_policy& access = series.preview_access();
                    view.source = source;
                    view.access = &access;
                    view.cache = series.preview_matches_main()
                        ? &m_impl->view.v_range_cache
                        : &m_impl->view.preview_v_range_cache;
                    return true;
                };

                if (v_auto) {
                if (auto_mode == Auto_v_range_mode::VISIBLE) {
                    auto [auto_v0, auto_v1] = compute_visible_v_range(
                        series_snapshot,
                        m_impl->view.v_range_cache,
                        nullptr,
                        resolve_main,
                            profiler,
                            m_impl->snapshot.cfg.t_min,
                            m_impl->snapshot.cfg.t_max,
                            usable_width,
                            m_impl->snapshot.cfg.v_min,
                            m_impl->snapshot.cfg.v_max);
                        v0 = auto_v0;
                        v1 = auto_v1;
                        apply_auto_padding(v0, v1);
                    }
                else {
                    auto [auto_v0, auto_v1] = compute_global_v_range(
                        series_snapshot,
                        m_impl->view.v_range_cache,
                        nullptr,
                        resolve_main,
                            profiler,
                            auto_mode == Auto_v_range_mode::GLOBAL_LOD,
                            m_impl->snapshot.cfg.v_min,
                            m_impl->snapshot.cfg.v_max);
                        v0 = auto_v0;
                        v1 = auto_v1;
                        apply_auto_padding(v0, v1);
                    }
                }
                else {
                    v0 = m_impl->snapshot.cfg.v_manual_min;
                    v1 = m_impl->snapshot.cfg.v_manual_max;
                }

            if (preview_enabled) {
                const bool preview_use_lod_cache = (auto_mode == Auto_v_range_mode::GLOBAL_LOD);
                auto [auto_preview_v0, auto_preview_v1] = compute_global_v_range(
                    series_snapshot,
                    m_impl->view.v_range_cache,
                    &m_impl->view.preview_v_range_cache,
                    resolve_preview,
                        profiler,
                        preview_use_lod_cache,
                        m_impl->snapshot.cfg.v_min,
                        m_impl->snapshot.cfg.v_max);
                    preview_v0 = auto_preview_v0;
                    preview_v1 = auto_preview_v1;
                    apply_auto_padding(preview_v0, preview_v1);
                }
                else {
                    preview_v0 = v0;
                    preview_v1 = v1;
                }
            }
            else if (v_auto) {
                v0 = m_impl->snapshot.cfg.v_min;
                v1 = m_impl->snapshot.cfg.v_max;
                preview_v0 = v0;
                preview_v1 = v1;
            }
            else {
                v0 = m_impl->snapshot.cfg.v_manual_min;
                v1 = m_impl->snapshot.cfg.v_manual_max;
                preview_v0 = v0;
                preview_v1 = v1;
            }

            // Update throttle cache.
            m_impl->view.cached_v0 = v0;
            m_impl->view.cached_v1 = v1;
            m_impl->view.cached_preview_v0 = preview_v0;
            m_impl->view.cached_preview_v1 = preview_v1;
            m_impl->view.range_cache_valid = true;
            m_impl->view.last_range_calc_time = now;
            m_impl->view.last_range_key = current_key;
        }

        if (v_auto) {
            // Animate v-range smoothly.
            const float target_v0 = v0;
            const float target_v1 = v1;
            const float target_preview_v0 = preview_v0;
            const float target_preview_v1 = preview_v1;
            const auto anim_now = std::chrono::steady_clock::now();
            if (!m_impl->view.anim_initialized) {
                const bool auto_just_enabled = v_auto && !prev_v_auto;
                if (auto_just_enabled) {
                    // When v_auto transitions from false to true, initialize
                    // animation from the manual range (what the user was viewing)
                    // to provide a smooth transition back to auto range.
                    const float manual_v0 = m_impl->snapshot.cfg.v_manual_min;
                    const float manual_v1 = m_impl->snapshot.cfg.v_manual_max;
                    if (std::isfinite(manual_v0) && std::isfinite(manual_v1) &&
                        manual_v0 < manual_v1)
                    {
                        m_impl->view.anim_v0 = manual_v0;
                        m_impl->view.anim_v1 = manual_v1;
                    }
                    else {
                        m_impl->view.anim_v0 = v0;
                        m_impl->view.anim_v1 = v1;
                    }
                }
                else {
                    m_impl->view.anim_v0 = v0;
                    m_impl->view.anim_v1 = v1;
                }
                // Preview can start from current auto (it was always auto-ranging)
                m_impl->view.anim_preview_v0 = preview_v0;
                m_impl->view.anim_preview_v1 = preview_v1;
                m_impl->view.last_anim_time = anim_now;
                m_impl->view.anim_initialized = true;
            }
            else {
                const float dt = std::chrono::duration<float>(anim_now - m_impl->view.last_anim_time).count();
                m_impl->view.last_anim_time = anim_now;
                const float t = 1.0f - std::exp(-impl_t::view_state_t::k_v_anim_speed * dt);
                m_impl->view.anim_v0         += (v0 - m_impl->view.anim_v0) * t;
                m_impl->view.anim_v1         += (v1 - m_impl->view.anim_v1) * t;
                m_impl->view.anim_preview_v0 += (preview_v0 - m_impl->view.anim_preview_v0) * t;
                m_impl->view.anim_preview_v1 += (preview_v1 - m_impl->view.anim_preview_v1) * t;
            }
            v0 = m_impl->view.anim_v0;
            v1 = m_impl->view.anim_v1;
            preview_v0 = m_impl->view.anim_preview_v0;
            preview_v1 = m_impl->view.anim_preview_v1;

            if (m_impl->owner) {
                // Sync auto range back to the widget so QML reads match the render range.
                if (std::abs(v0 - m_impl->snapshot.cfg.v_min) > k_auto_v_sync_eps ||
                    std::abs(v1 - m_impl->snapshot.cfg.v_max) > k_auto_v_sync_eps)
                {
                    vnm::post_invoke(
                        const_cast<Plot_widget*>(m_impl->owner),
                        &Plot_widget::set_auto_v_range_from_renderer,
                        v0, v1);
                }
            }

            // Request another frame if still animating.
            const float span = std::max(std::abs(target_v1 - target_v0), k_anim_span_min);
            const bool main_animating =
                std::abs(v0 - target_v0) > span * k_anim_target_frac ||
                std::abs(v1 - target_v1) > span * k_anim_target_frac;
            const bool preview_visible = preview_enabled;
            const float preview_span =
                std::max(std::abs(target_preview_v1 - target_preview_v0), k_anim_span_min);
            const bool preview_animating = preview_visible &&
                (std::abs(preview_v0 - target_preview_v0) > preview_span * k_anim_target_frac ||
                 std::abs(preview_v1 - target_preview_v1) > preview_span * k_anim_target_frac);
            const bool still_animating = main_animating || preview_animating;
            if (still_animating && m_impl->owner) {
                vnm::post_invoke(
                    const_cast<Plot_widget*>(m_impl->owner),
                    &Plot_widget::update);
            }
        }
        else {
            m_impl->view.anim_initialized = false;
        }
    }

    if (m_impl->owner) {
        m_impl->owner->set_rendered_v_range(v0, v1);
    }

    double prev_v_span = 0.0;
    double prev_t_span = 0.0;
    double v_span = 0.0;
    double t_span = 0.0;
    bool fade_v_labels = false;
    bool fade_h_labels = false;
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.layout_prep");
        prev_v_span = m_impl->view.last_vertical_span;
        prev_t_span = m_impl->view.last_horizontal_span;
        v_span = double(v1) - double(v0);
        t_span = m_impl->snapshot.cfg.t_max - m_impl->snapshot.cfg.t_min;

        fade_v_labels = (prev_v_span > 0.0) && !spans_approx_equal(v_span, prev_v_span);
        fade_h_labels = (prev_t_span > 0.0) && !spans_approx_equal(t_span, prev_t_span);
    }

    const frame_layout_result_t& frame_layout = [&]() -> const frame_layout_result_t& {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.calculate_layout");
        return m_impl->calculate_frame_layout(
            v0,
            v1,
            m_impl->snapshot.cfg.t_min,
            m_impl->snapshot.cfg.t_max,
            win_w,
            win_h);
    }();
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.layout_finalize");
        notify_hlabels_subsecond(frame_layout.h_labels_subsecond);
        m_impl->update_seed_history(v_span, t_span, frame_layout);
    }

    frame_context_t core_ctx = [&]() -> frame_context_t {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.context_build");
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.context_build.core_ctx");
        Frame_context_builder builder(frame_layout);
        builder
            .v_range(v0, v1)
            .preview_v_range(preview_v0, preview_v1)
            .t_range(m_impl->snapshot.cfg.t_min, m_impl->snapshot.cfg.t_max)
            .available_t_range(m_impl->snapshot.cfg.t_available_min, m_impl->snapshot.cfg.t_available_max)
            .window_size(win_w, win_h)
            .pmv(glm::ortho(0.f, float(win_w), float(win_h), 0.f, -1.f, 1.f))
            .font_px(m_impl->snapshot.adjusted_font_px, m_impl->snapshot.base_label_height_px)
            .reserved_heights(m_impl->snapshot.adjusted_reserved_height, m_impl->snapshot.adjusted_preview_height)
            .show_info(m_impl->snapshot.show_info)
            .config(config);
        return builder.build();
    }();

    // Clear to transparent - let QML provide the background color (matches Lumis behavior)
    const bool dark_mode = config ? config->dark_mode : false;
    const bool clear_to_transparent = config ? config->clear_to_transparent : false;
    const Color_palette palette =
        dark_mode ? Color_palette::dark() : Color_palette::light();
    const bool skip_gl_calls = config && config->skip_gl_calls;
    GLboolean was_multisample = GL_FALSE;
    if (!skip_gl_calls) {
        if (clear_to_transparent) {
            glClearColor(0.f, 0.f, 0.f, 0.f);
        }
        else {
            glClearColor(
                palette.background.r,
                palette.background.g,
                palette.background.b,
                palette.background.a);
        }
        glClear(GL_COLOR_BUFFER_BIT);

        was_multisample = glIsEnabled(GL_MULTISAMPLE);
        if (!was_multisample) {
            glEnable(GL_MULTISAMPLE);
        }

        // Enable blending
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Render chrome (backgrounds, grid)
    m_impl->chrome.render_grid_and_backgrounds(core_ctx, m_impl->primitives);

    // Render series
    if (m_impl->owner && !series_snapshot.empty()) {
        m_impl->series.render(core_ctx, series_snapshot);
    }

    // Always draw the zero-value gridline on top of series
    m_impl->chrome.render_zero_line(core_ctx, m_impl->primitives);

    // Render preview overlay
    if (preview_enabled) {
        m_impl->chrome.render_preview_overlay(core_ctx, m_impl->primitives);
    }
    if (!skip_gl_calls) {
        m_impl->primitives.flush_rects(core_ctx.pmv);
    }

    // Render text labels
    if (!skip_gl_calls && m_impl->text && (!config || config->show_text)) {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.text_overlay");
        const bool fades_active = m_impl->text->render(core_ctx, fade_v_labels, fade_h_labels);
        if (fades_active && m_impl->owner) {
            vnm::post_invoke(
                const_cast<Plot_widget*>(m_impl->owner),
                &Plot_widget::update);
        }
    }

    if (!skip_gl_calls) {
        glDisable(GL_BLEND);
        if (!was_multisample) {
            glDisable(GL_MULTISAMPLE);
        }
    }
}

QOpenGLFramebufferObject* Plot_renderer::createFramebufferObject(const QSize& size)
{
    m_impl->viewport_width = size.width();
    m_impl->viewport_height = size.height();

    QOpenGLFramebufferObjectFormat format;
    format.setSamples(k_msaa_samples);
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setInternalTextureFormat(GL_RGBA8);
    return new QOpenGLFramebufferObject(size, format);
}

} // namespace vnm::plot
