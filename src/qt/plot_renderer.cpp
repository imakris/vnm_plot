#include <vnm_plot/qt/plot_renderer.h>
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/layout_calculator.h>
#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/asset_loader.h>
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
    return diff <= scale * 1e-6;
}

struct lod_minmax_cache_t
{
    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    uint64_t sequence = 0;
    bool valid = false;
};

struct series_minmax_cache_t
{
    const void* identity = nullptr;
    std::vector<lod_minmax_cache_t> lods;
};

std::string normalize_asset_name(std::string_view name)
{
    std::string_view out = name;
    if (out.rfind("qrc:/", 0) == 0) {
        out.remove_prefix(5);
    }
    else if (out.rfind(":/", 0) == 0) {
        out.remove_prefix(2);
    }
    if (out.rfind("vnm_plot/", 0) == 0) {
        out.remove_prefix(9);
    }
    // Map legacy glsl/ paths to embedded shaders/ assets.
    // The trade_to_plot vertex shader uses the same Sample output interface as function_sample.
    if (out == "glsl/trade_to_plot.vert") {
        return "shaders/function_sample.vert";
    }
    if (out == "glsl/plot_area.geom") {
        return "shaders/plot_area.geom";
    }
    if (out == "glsl/plot_area.frag") {
        return "shaders/plot_area.frag";
    }
    return std::string(out);
}

shader_set_t normalize_shader_set(const shader_set_t& shader)
{
    shader_set_t res;
    res.vert = normalize_asset_name(shader.vert);
    res.geom = normalize_asset_name(shader.geom);
    res.frag = normalize_asset_name(shader.frag);
    return res;
}

bool compute_snapshot_minmax(
    const series_data_t& series,
    const data_snapshot_t& snapshot,
    float& out_min,
    float& out_max)
{
    if (!series.access.get_value && !series.access.get_range) {
        return false;
    }

    if (!snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        return false;
    }

    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    const auto* base = static_cast<const std::uint8_t*>(snapshot.data);
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const void* sample = base + i * snapshot.stride;

        float low, high;
        if (series.access.get_range) {
            auto [lo, hi] = series.get_range(sample);
            low = lo;
            high = hi;
        }
        else {
            low = series.get_value(sample);
            high = low;
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
    const series_data_t& series,
    const data_snapshot_t& snapshot,
    double t_min,
    double t_max,
    std::size_t& start_idx,
    std::size_t& end_idx)
{
    start_idx = 0;
    end_idx = snapshot.count;

    if (!series.access.get_timestamp || !snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        return false;
    }

    if (!(t_max > t_min)) {
        return true;
    }

    const auto* base = static_cast<const std::uint8_t*>(snapshot.data);
    const double first_ts = series.get_timestamp(base);
    const double last_ts = series.get_timestamp(base + (snapshot.count - 1) * snapshot.stride);
    if (std::isfinite(first_ts) && std::isfinite(last_ts)) {
        if (t_max < first_ts || t_min > last_ts) {
            return false;
        }
    }

    std::size_t lo = 0;
    std::size_t hi = snapshot.count;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const double ts = series.get_timestamp(base + mid * snapshot.stride);
        if (ts < t_min) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    start_idx = (lo > 0) ? (lo - 1) : 0;

    lo = start_idx;
    hi = snapshot.count;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const double ts = series.get_timestamp(base + mid * snapshot.stride);
        if (ts <= t_max) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    end_idx = std::min(lo + 1, snapshot.count);
    if (end_idx <= start_idx) {
        end_idx = std::min(start_idx + 1, snapshot.count);
    }

    return true;
}

bool compute_window_minmax(
    const series_data_t& series,
    const data_snapshot_t& snapshot,
    std::size_t start_idx,
    std::size_t end_idx,
    float& out_min,
    float& out_max)
{
    if (!series.access.get_value && !series.access.get_range) {
        return false;
    }

    if (!snapshot || snapshot.count == 0 || snapshot.stride == 0) {
        return false;
    }

    if (end_idx <= start_idx || start_idx >= snapshot.count) {
        return false;
    }

    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    const auto* base = static_cast<const std::uint8_t*>(snapshot.data);
    for (std::size_t i = start_idx; i < end_idx; ++i) {
        const void* sample = base + i * snapshot.stride;

        float low = 0.0f;
        float high = 0.0f;
        if (series.access.get_range) {
            auto [lo, hi] = series.get_range(sample);
            low = lo;
            high = hi;
        }
        else {
            low = series.get_value(sample);
            high = low;
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

bool get_lod_minmax(
    const series_data_t& series,
    series_minmax_cache_t& cache,
    std::size_t level,
    float& out_min,
    float& out_max)
{
    if (!series.data_source) {
        return false;
    }

    if (level >= cache.lods.size()) {
        return false;
    }

    auto& entry = cache.lods[level];

    auto snapshot = series.data_source->snapshot(level);
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
    if (!compute_snapshot_minmax(series, snapshot, v_min, v_max)) {
        return false;
    }

    entry.v_min = v_min;
    entry.v_max = v_max;
    entry.sequence = snapshot.sequence;
    entry.valid = true;

    out_min = v_min;
    out_max = v_max;
    return true;
}

std::pair<float, float> compute_global_v_range(
    const std::map<int, std::shared_ptr<series_data_t>>& series_map,
    std::unordered_map<int, series_minmax_cache_t>& cache_map,
    bool use_lod_cache,
    float fallback_min,
    float fallback_max)
{
    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    std::unordered_set<int> active_ids;

    for (const auto& [id, series] : series_map) {
        if (!series || !series->enabled || !series->data_source) {
            continue;
        }
        if (!series->access.get_value && !series->access.get_range) {
            continue;
        }

        active_ids.insert(id);

        float series_min = 0.0f;
        float series_max = 0.0f;
        bool got_range = false;

        // Fast path: data source provides O(1) range query.
        if (series->data_source->has_value_range() &&
            !series->data_source->value_range_needs_rescan())
        {
            auto [ds_min, ds_max] = series->data_source->value_range();
            if (std::isfinite(ds_min) && std::isfinite(ds_max) && ds_min <= ds_max) {
                series_min = ds_min;
                series_max = ds_max;
                got_range = true;
            }
        }

        if (!got_range) {
            // Slow path: scan LOD data with caching.
            series_minmax_cache_t& cache = cache_map[id];
            const void* identity = series->data_source->identity();
            const std::size_t levels = series->data_source->lod_levels();
            if (levels == 0) {
                continue;
            }

            if (cache.identity != identity || cache.lods.size() != levels) {
                cache.identity = identity;
                cache.lods.assign(levels, lod_minmax_cache_t{});
            }

            const std::size_t level = use_lod_cache ? (levels - 1) : 0;
            if (!get_lod_minmax(*series, cache, level, series_min, series_max)) {
                if (level == 0 || !get_lod_minmax(*series, cache, 0, series_min, series_max)) {
                    continue;
                }
            }
        }

        v_min = std::min(v_min, series_min);
        v_max = std::max(v_max, series_max);
        have_any = true;
    }

    for (auto it = cache_map.begin(); it != cache_map.end();) {
        if (active_ids.find(it->first) == active_ids.end()) {
            it = cache_map.erase(it);
        }
        else {
            ++it;
        }
    }

    if (!have_any) {
        return {fallback_min, fallback_max};
    }

    return {v_min, v_max};
}

std::pair<float, float> compute_visible_v_range(
    const std::map<int, std::shared_ptr<series_data_t>>& series_map,
    std::unordered_map<int, series_minmax_cache_t>& cache_map,
    double t_min,
    double t_max,
    double width_px,
    float fallback_min,
    float fallback_max)
{
    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    std::unordered_set<int> active_ids;

    for (const auto& [id, series] : series_map) {
        if (!series || !series->enabled || !series->data_source) {
            continue;
        }
        if (!series->access.get_timestamp) {
            continue;
        }
        if (!series->access.get_value && !series->access.get_range) {
            continue;
        }

        active_ids.insert(id);

        const std::size_t levels = series->data_source->lod_levels();
        if (levels == 0) {
            continue;
        }

        auto snapshot0 = series->data_source->snapshot(0);
        if (!snapshot0 || snapshot0.count == 0 || snapshot0.stride == 0) {
            continue;
        }

        std::size_t start0 = 0;
        std::size_t end0 = 0;
        if (!find_window_indices(*series, snapshot0, t_min, t_max, start0, end0)) {
            continue;
        }
        const std::size_t count0 = (end0 > start0) ? (end0 - start0) : 0;
        if (count0 == 0) {
            continue;
        }

        const std::vector<std::size_t> scales = compute_lod_scales(*series->data_source);
        const std::size_t base_scale = scales.empty() ? 1 : scales[0];
        const std::size_t base_samples = count0 * base_scale;
        const double base_pps = (base_samples > 0 && width_px > 0.0)
            ? width_px / static_cast<double>(base_samples)
            : 0.0;
        const std::size_t desired_level = choose_lod_level(scales, 0, base_pps);

        std::size_t applied_level = desired_level;
        auto snapshot = series->data_source->snapshot(applied_level);
        if (!snapshot || snapshot.count == 0 || snapshot.stride == 0) {
            snapshot = snapshot0;
            applied_level = 0;
        }

        std::size_t start = 0;
        std::size_t end = 0;
        if (!find_window_indices(*series, snapshot, t_min, t_max, start, end)) {
            continue;
        }

        float series_min = 0.0f;
        float series_max = 0.0f;
        const bool full_range = (start == 0 && end == snapshot.count);

        series_minmax_cache_t& cache = cache_map[id];
        const void* identity = series->data_source->identity();
        if (cache.identity != identity || cache.lods.size() != levels) {
            cache.identity = identity;
            cache.lods.assign(levels, lod_minmax_cache_t{});
        }

        if (full_range) {
            if (!get_lod_minmax(*series, cache, applied_level, series_min, series_max)) {
                continue;
            }
        }
        else {
            if (!compute_window_minmax(
                    *series,
                    snapshot,
                    start,
                    end,
                    series_min,
                    series_max)) {
                continue;
            }
        }

        v_min = std::min(v_min, series_min);
        v_max = std::max(v_max, series_max);
        have_any = true;
    }

    for (auto it = cache_map.begin(); it != cache_map.end();) {
        if (active_ids.find(it->first) == active_ids.end()) {
            it = cache_map.erase(it);
        }
        else {
            ++it;
        }
    }

    if (!have_any) {
        return {fallback_min, fallback_max};
    }

    const float span = v_max - v_min;
    const float padding = (span > 0.0f) ? span * 0.05f : 0.5f;
    return {v_min - padding, v_max + padding};
}

} // namespace

struct Plot_renderer::impl_t
{
    struct render_snapshot_t
    {
        data_config_t cfg;
        bool visible = false;
        bool show_info = false;
        bool v_auto = true;

        Plot_config config;

        std::map<int, std::shared_ptr<Data_source>> data;

        double adjusted_font_px = 12.0;
        double base_label_height_px = 14.0;
        double vbar_width_pixels = 0.0;
        double adjusted_reserved_height = 0.0;
        double adjusted_preview_height = 0.0;
    };

    // Field order optimized to minimize padding (clang-tidy warning).
    Asset_loader asset_loader;
    Primitive_renderer primitives;
    render_snapshot_t snapshot;
    const Plot_widget* owner = nullptr;
    Font_renderer fonts;
    std::unique_ptr<Text_renderer> text;

    double last_vertical_span = 0.0;
    double last_vertical_seed_step = 0.0;
    double last_horizontal_span = 0.0;
    double last_horizontal_seed_step = 0.0;

    // Range throttling state (Phase 1 optimization).
    // Reduces range_calc from render rate (~19 Hz) to throttled rate (~10 Hz).
    // NOTE: Data sequence changes within the throttle interval may cause up to
    // 100ms staleness. The internal per-LOD cache in compute_*_v_range handles
    // sequence-based invalidation, so this is mainly a loop/call overhead skip.
    std::chrono::steady_clock::time_point last_range_calc_time{};
    double last_range_t_min = 0.0;
    double last_range_t_max = 0.0;
    double last_range_extra_scale = 0.0;
    float cached_v0 = 0.0f;
    float cached_v1 = 0.0f;
    float cached_preview_v0 = 0.0f;
    float cached_preview_v1 = 0.0f;
    Auto_v_range_mode last_range_auto_mode = Auto_v_range_mode::GLOBAL;
    bool range_cache_valid = false;
    bool last_range_v_auto = true;
    static constexpr std::chrono::milliseconds k_range_throttle_interval{100};

    std::unordered_map<int, std::shared_ptr<series_data_t>> core_series_cache;
    std::unordered_map<int, series_minmax_cache_t> v_range_cache;

    Layout_calculator layout_calc;
    Series_renderer series;
    Layout_cache layout_cache;

    int init_failed_status = 0;
    int last_font_px = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    int last_opengl_status = std::numeric_limits<int>::min();
    int last_hlabels_subsecond = -1;
    int last_vertical_seed_index = -1;
    int last_horizontal_seed_index = -1;
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

    if (const auto* cached = layout_cache.try_get(cache_key)) {
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
            last_vertical_seed_index >= 0 &&
            spans_approx_equal(v_span, last_vertical_span) &&
            last_vertical_seed_step > 0.0)
        {
            layout_params.has_vertical_seed = true;
            layout_params.vertical_seed_index = last_vertical_seed_index;
            layout_params.vertical_seed_step = last_vertical_seed_step;
        }

        if (t_span > 0.0 &&
            last_horizontal_seed_index >= 0 &&
            spans_approx_equal(t_span, last_horizontal_span) &&
            last_horizontal_seed_step > 0.0)
        {
            layout_params.has_horizontal_seed = true;
            layout_params.horizontal_seed_index = last_horizontal_seed_index;
            layout_params.horizontal_seed_step = last_horizontal_seed_step;
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

    const double measured_vbar_width = std::max(
        k_vbar_min_width_px_d,
        double(layout_result.max_v_label_text_width) + k_v_label_horizontal_padding_px);

    // If measured width differs significantly, notify widget to animate towards it.
    // Continue using the current animated width for this frame - the animation will
    // progress smoothly on subsequent frames via the widget's timer.
    double effective_vbar_width = snapshot.vbar_width_pixels;
    if (std::abs(snapshot.vbar_width_pixels - measured_vbar_width) > k_vbar_width_change_threshold_d)
    {
        // Notify widget to animate to new width
        if (owner) {
            QMetaObject::invokeMethod(
                const_cast<Plot_widget*>(owner),
                "set_vbar_width_from_renderer",
                Qt::QueuedConnection,
                Q_ARG(double, measured_vbar_width));
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
        return layout_cache.store(cache_key, std::move(frame_layout));
    }
}

void Plot_renderer::impl_t::update_seed_history(
    double v_span,
    double t_span,
    const frame_layout_result_t& layout)
{
    if (v_span > 0.0) {
        last_vertical_span = v_span;
        last_vertical_seed_index = layout.vertical_seed_index;
        last_vertical_seed_step = layout.vertical_seed_step;
    }
    else {
        last_vertical_span = 0.0;
        last_vertical_seed_index = -1;
        last_vertical_seed_step = 0.0;
    }

    if (t_span > 0.0) {
        last_horizontal_span = t_span;
        last_horizontal_seed_index = layout.horizontal_seed_index;
        last_horizontal_seed_step = layout.horizontal_seed_step;
    }
    else {
        last_horizontal_span = 0.0;
        last_horizontal_seed_index = -1;
        last_horizontal_seed_step = 0.0;
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

    // Copy series data
    {
        std::shared_lock lock(widget->m_series_mutex);
        // Convert series to data sources
        m_impl->snapshot.data.clear();
        for (const auto& [id, series] : widget->m_series) {
            if (series && series->data_source) {
                m_impl->snapshot.data[id] = series->data_source;
            }
        }
    }

    // Copy UI state
    m_impl->snapshot.visible = (widget->width() > 0.0 && widget->height() > 0.0);
    m_impl->snapshot.show_info = widget->m_show_info.load(std::memory_order_acquire);
    m_impl->snapshot.v_auto = widget->m_v_auto.load(std::memory_order_acquire);
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

    float v0 = 0.0f;
    float v1 = 0.0f;
    float preview_v0 = 0.0f;
    float preview_v1 = 0.0f;
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.range_calc");

        const auto now = std::chrono::steady_clock::now();
        const bool throttle_expired =
            (now - m_impl->last_range_calc_time) >= impl_t::k_range_throttle_interval;

        // Calculate layout
        const double usable_width = win_w - m_impl->snapshot.vbar_width_pixels;

        // Determine v-range mode and config.
        const Auto_v_range_mode auto_mode =
            config ? config->auto_v_range_mode : Auto_v_range_mode::GLOBAL;
        const double auto_v_extra_scale = config ? config->auto_v_range_extra_scale : 0.0;
        const bool v_auto = m_impl->snapshot.v_auto;
        const bool can_use_series = (m_impl->owner != nullptr);

        // Check for cache invalidation conditions.
        // Invalidate when: mode changes, v_auto changes, config changes, or window changes.
        const bool mode_changed = (auto_mode != m_impl->last_range_auto_mode) ||
                                  (v_auto != m_impl->last_range_v_auto);

        const bool config_changed = (auto_v_extra_scale != m_impl->last_range_extra_scale);

        const bool window_changed = (auto_mode == Auto_v_range_mode::VISIBLE) &&
                                    ((m_impl->snapshot.cfg.t_min != m_impl->last_range_t_min) ||
                                     (m_impl->snapshot.cfg.t_max != m_impl->last_range_t_max));

        const bool cache_invalid = mode_changed || config_changed || window_changed ||
                                   !m_impl->range_cache_valid;

        if (!cache_invalid && !throttle_expired) {
            // Use cached values (throttled path).
            if (v_auto) {
                v0 = m_impl->cached_v0;
                v1 = m_impl->cached_v1;
            }
            else {
                // When v_auto is false, always use manual config directly.
                v0 = m_impl->snapshot.cfg.v_manual_min;
                v1 = m_impl->snapshot.cfg.v_manual_max;
            }
            preview_v0 = m_impl->cached_preview_v0;
            preview_v1 = m_impl->cached_preview_v1;
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

            bool preview_use_lod_cache = false;
            if (can_use_series) {
                std::shared_lock lock(m_impl->owner->m_series_mutex);
                if (v_auto) {
                    if (auto_mode == Auto_v_range_mode::VISIBLE) {
                        auto [auto_v0, auto_v1] = compute_visible_v_range(
                            m_impl->owner->m_series,
                            m_impl->v_range_cache,
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
                            m_impl->owner->m_series,
                            m_impl->v_range_cache,
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

                preview_use_lod_cache = (auto_mode == Auto_v_range_mode::GLOBAL_LOD);
                auto [auto_preview_v0, auto_preview_v1] = compute_global_v_range(
                    m_impl->owner->m_series,
                    m_impl->v_range_cache,
                    preview_use_lod_cache,
                    m_impl->snapshot.cfg.v_min,
                    m_impl->snapshot.cfg.v_max);
                preview_v0 = auto_preview_v0;
                preview_v1 = auto_preview_v1;
                apply_auto_padding(preview_v0, preview_v1);
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
            m_impl->cached_v0 = v0;
            m_impl->cached_v1 = v1;
            m_impl->cached_preview_v0 = preview_v0;
            m_impl->cached_preview_v1 = preview_v1;
            m_impl->range_cache_valid = true;
            m_impl->last_range_calc_time = now;
            m_impl->last_range_auto_mode = auto_mode;
            m_impl->last_range_v_auto = v_auto;
            m_impl->last_range_extra_scale = auto_v_extra_scale;
            m_impl->last_range_t_min = m_impl->snapshot.cfg.t_min;
            m_impl->last_range_t_max = m_impl->snapshot.cfg.t_max;
        }

        if (v_auto && m_impl->owner) {
            // Sync auto range back to the widget so QML reads match the render range.
            constexpr float k_auto_v_eps = 1e-6f;
            if (std::abs(v0 - m_impl->snapshot.cfg.v_min) > k_auto_v_eps ||
                std::abs(v1 - m_impl->snapshot.cfg.v_max) > k_auto_v_eps)
            {
                QMetaObject::invokeMethod(
                    const_cast<Plot_widget*>(m_impl->owner),
                    "set_auto_v_range_from_renderer",
                    Qt::QueuedConnection,
                    Q_ARG(float, v0),
                    Q_ARG(float, v1));
            }
        }
    }

    double prev_v_span = 0.0;
    double prev_t_span = 0.0;
    double v_span = 0.0;
    double t_span = 0.0;
    bool fade_v_labels = false;
    bool fade_h_labels = false;
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.layout_prep");
        prev_v_span = m_impl->last_vertical_span;
        prev_t_span = m_impl->last_horizontal_span;
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

    Render_config core_config;
    frame_context_t core_ctx = [&]() -> frame_context_t {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.context_build");
        if (config) {
            VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.context_build.config");
            core_config.dark_mode = config->dark_mode;
            core_config.show_text = config->show_text;
            core_config.snap_lines_to_pixels = config->snap_lines_to_pixels;
            core_config.line_width_px = config->line_width_px;
            core_config.area_fill_alpha = config->area_fill_alpha;
            core_config.format_timestamp = config->format_timestamp
                ? config->format_timestamp
                : default_format_timestamp;
            core_config.log_debug = config->log_debug;
            core_config.log_error = config->log_error;
            core_config.profiler = profiler;
        }

        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.context_build.core_ctx");
        return frame_context_t{
            frame_layout,
            v0,
            v1,
            preview_v0,
            preview_v1,
            m_impl->snapshot.cfg.t_min,
            m_impl->snapshot.cfg.t_max,
            m_impl->snapshot.cfg.t_available_min,
            m_impl->snapshot.cfg.t_available_max,
            win_w,
            win_h,
            glm::ortho(0.f, float(win_w), float(win_h), 0.f, -1.f, 1.f),
            m_impl->snapshot.adjusted_font_px,
            m_impl->snapshot.base_label_height_px,
            m_impl->snapshot.adjusted_reserved_height,
            m_impl->snapshot.adjusted_preview_height,
            m_impl->snapshot.show_info,
            config ? &core_config : nullptr
        };
    }();

    frame_context_t series_ctx = [&]() -> frame_context_t {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.context_build.series_ctx");
        return core_ctx;
    }();

    // Clear to transparent - let QML provide the background color (matches Lumis behavior)
    const bool dark_mode = config ? config->dark_mode : false;
    const bool clear_to_transparent = config ? config->clear_to_transparent : false;
    const Color_palette palette =
        dark_mode ? Color_palette::dark() : Color_palette::light();
    GLboolean was_multisample = GL_FALSE;
    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.gl_setup");
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
    if (m_impl->owner) {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.execute_passes");
        std::shared_lock lock(m_impl->owner->m_series_mutex, std::defer_lock);
        {
            VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.execute_passes.lock");
            lock.lock();
        }
        std::map<int, std::shared_ptr<series_data_t>> core_series_map;
        std::unordered_set<int> seen_ids;
        {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.execute_passes.series_prepare");
            core_series_map.clear();
            for (const auto& [id, series] : m_impl->owner->m_series) {
                if (!series) {
                    continue;
                }

                seen_ids.insert(id);

                auto& core_series = m_impl->core_series_cache[id];
                if (!core_series) {
                    core_series = std::make_shared<series_data_t>();
                }

                // Copy basic fields (types are now unified, no conversion needed)
                core_series->id = id;
                core_series->enabled = series->enabled;
                core_series->style = series->style;
                core_series->color = series->color;
                core_series->colormap = series->colormap;

                // Normalize shader paths (remove qrc:/ prefixes for embedded asset lookup)
                core_series->shader_set = normalize_shader_set(series->shader_set);
                core_series->shaders.clear();
                for (const auto& [style, shader] : series->shaders) {
                    core_series->shaders.emplace(style, normalize_shader_set(shader));
                }

                // Copy access policy (types are now unified)
                core_series->access = series->access;

                // Use data source directly (types are now unified, no adapter needed)
                core_series->data_source = series->data_source;

                core_series_map[id] = core_series;
            }

            // Cleanup stale cache entries
            for (auto it = m_impl->core_series_cache.begin(); it != m_impl->core_series_cache.end(); ) {
                if (seen_ids.find(it->first) == seen_ids.end()) {
                    it = m_impl->core_series_cache.erase(it);
                }
                else {
                    ++it;
                }
            }
        }

        {
            VNM_PLOT_PROFILE_SCOPE(
                profiler,
                "renderer.frame.execute_passes.series_render");
            m_impl->series.render(series_ctx, core_series_map);
        }
    }

    // Render preview overlay
    m_impl->chrome.render_preview_overlay(core_ctx, m_impl->primitives);
    m_impl->primitives.flush_rects(core_ctx.pmv);

    // Render text labels
    if (m_impl->text && (!config || config->show_text)) {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.text_overlay");
        const bool fades_active = m_impl->text->render(core_ctx, fade_v_labels, fade_h_labels);
        if (fades_active && m_impl->owner) {
            QMetaObject::invokeMethod(
                const_cast<Plot_widget*>(m_impl->owner),
                "update",
                Qt::QueuedConnection);
        }
    }

    {
        VNM_PLOT_PROFILE_SCOPE(profiler, "renderer.frame.gl_cleanup");
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
