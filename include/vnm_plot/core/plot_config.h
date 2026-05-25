#pragma once

// VNM Plot Library - Configuration
// Injectable configuration for application-specific behavior.
// This allows vnm_plot to work without downstream dependencies while
// still being customizable by the host application.

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace vnm::plot {

class Asset_loader;

enum class Value_format_role
{
    AXIS_LABEL,
    INDICATOR,
    INFO_OVERLAY,
};

struct value_format_context_t
{
    Value_format_role role = Value_format_role::AXIS_LABEL;
    int suggested_fixed_digits = 0;
    std::string_view series_label;
};

// -----------------------------------------------------------------------------
// Auto V-Range Mode
// -----------------------------------------------------------------------------
// Controls how the auto v-range is computed when v_auto is enabled.
enum class Auto_v_range_mode
{
    // Default: global full-resolution range.
    GLOBAL,
    // Global range using per-LOD min/max caches (fast, may use coarse LOD).
    GLOBAL_LOD,
    // Visible time window only (uses LOD selection for speed).
    VISIBLE
};

// -----------------------------------------------------------------------------
// Profiling Interface (optional)
// -----------------------------------------------------------------------------
// Applications can inject profiling by implementing this interface.
// If not provided, profiling is a no-op.
class Profiler
{
public:
    virtual ~Profiler() = default;
    virtual void begin_scope(const char* name) = 0;
    virtual void end_scope() = 0;
    virtual void record_observation(const char* name, double value_ms) { (void) name; (void) value_ms; }
};

// RAII scope guard for profiling
class Profile_scope
{
public:
    Profile_scope(Profiler* profiler, const char* name)
    :
        m_profiler(profiler)
    {
        if (m_profiler) {
            m_profiler->begin_scope(name);
        }
    }

    ~Profile_scope()
    {
        if (m_profiler) {
            m_profiler->end_scope();
        }
    }

    Profile_scope(const Profile_scope&) = delete;
    Profile_scope& operator=(const Profile_scope&) = delete;

private:
    Profiler* m_profiler;
};

// Macro helpers for proper __LINE__ expansion
#define VNM_PLOT_CONCAT_IMPL(a, b) a##b
#define VNM_PLOT_CONCAT(a, b) VNM_PLOT_CONCAT_IMPL(a, b)

// Macro for scoped profiling (no-op if profiler is null)
#define VNM_PLOT_PROFILE_SCOPE(profiler, name) \
    ::vnm::plot::Profile_scope VNM_PLOT_CONCAT(vnm_plot_profile_scope_, __LINE__)((profiler), (name))

// -----------------------------------------------------------------------------
// Plot Configuration
// -----------------------------------------------------------------------------
// This struct contains all application-specific configuration that
// can be injected into vnm_plot components.
struct Plot_config
{
    // --- Theme ---
    bool dark_mode = false;
    bool show_text = true;
    double grid_visibility = 1.0;     // 0..1 alpha; 0 = hidden (skipped), 1 = fully visible
    double preview_visibility = 1.0;  // 0..1 alpha; 0 = hidden (skipped), 1 = fully visible

    // --- Timestamp Formatting ---
    // Callback to format timestamps for axis labels.
    // Parameters: timestamp_ns (int64 nanoseconds), step_ns (tick interval in
    // nanoseconds). Both are in the API's int64 nanosecond unit; converters to
    // seconds (or any other unit) live inside the formatter implementation.
    // Returns: formatted string for display
    // If null, a default formatter is used.
    std::function<std::string(std::int64_t timestamp_ns, std::int64_t step_ns)> format_timestamp;
    // Revision for formatter behavior. Caller contract: increment when the
    // effective output of format_timestamp changes without replacing the
    // callback identity (e.g. captured/stateful data updates).
    std::uint64_t format_timestamp_revision = 0;
    // Generic value formatter for Y-axis labels, indicator values, and info
    // overlay values. Applications own units, locale, and domain-specific
    // precision. If null, vnm_plot uses its neutral numeric defaults.
    std::function<std::string(double value, const value_format_context_t& context)> format_value;
    std::uint64_t format_value_revision = 0;

    // --- Profiling (optional) ---
    // If provided, profiling scopes will be recorded.
    std::shared_ptr<Profiler> profiler;

    // --- Font Configuration ---
    double font_size_px = 10.0;
    double base_label_height_px = 14.0;

    // --- Logging (optional) ---
    // Hook for debug messages (e.g., LOD selection).
    std::function<void(const std::string&)> log_debug;
    std::function<void(const std::string&)> log_error;

    // --- Asset Loading (optional) ---
    // Hook for registering additional embedded assets on the renderer loader.
    std::function<void(Asset_loader&)> register_assets;
    // Bump to force re-registration of assets (e.g., hot-swap shaders).
    std::uint32_t assets_revision = 0;

    // --- Preview Bar ---
    double preview_height_px = 0.0;  // 0 = auto, >0 = fixed height

    // --- Clear Behavior ---
    // When true, clear to transparent so QML controls the background.
    bool clear_to_transparent = false;

    // --- Line Rendering ---
    // When true, snap line vertices to pixel centers (can look jagged on
    // diagonals; default is false for smoother lines).
    bool snap_lines_to_pixels = false;
    // Line width in pixels (may be clamped by the driver).
    double line_width_px = 1.0;
    // Dot diameter in pixels for DOTS rendering. The shader floors at 1 px,
    // so values below 1.0 still render as a 1-pixel dot.
    double point_diameter_px = 1.0;
    // Area fill alpha multiplier (0..1).
    double area_fill_alpha = 0.3;

    // When true, renderer may request additional frames for smooth animations.
    // When false, renderer suppresses self-posted updates and may skip unchanged
    // frames based on a render signature to minimize idle CPU usage.
    bool allow_renderer_self_scheduling = false;

    // --- Auto V-Range ---
    // Default is GLOBAL.
    Auto_v_range_mode auto_v_range_mode = Auto_v_range_mode::GLOBAL;
    // Extra scale applied to auto v-range (0 = no padding).
    double auto_v_range_extra_scale = 0.0;
    // When true, padding cannot pull a nonnegative auto-computed range below zero.
    bool floor_nonnegative_auto_v_range_at_zero = false;

    // Maintenance aid: bump when adding a field so that comparators (e.g.
    // plot_config_equivalent) fail to compile until they are updated.
    static constexpr int field_count = 25;
};

// -----------------------------------------------------------------------------
// Default Timestamp Formatter
// -----------------------------------------------------------------------------
// Simple formatter when no custom one is provided.
// For full formatting with timezone support, applications should provide
// their own formatter via Plot_config::format_timestamp. Both inputs are in
// nanoseconds (API convention).
inline std::string default_format_timestamp(std::int64_t timestamp_ns, std::int64_t step_ns)
{
    // Simple formatting with step-appropriate precision.
    // Applications should override for timezone-aware formatting.
    constexpr std::int64_t k_ns_per_second = 1'000'000'000;
    constexpr std::int64_t k_ns_per_minute = 60 * k_ns_per_second;
    // Floor-divide so negative timestamps map to the matching second-of-epoch.
    const std::int64_t whole_seconds = (timestamp_ns >= 0)
        ? (timestamp_ns / k_ns_per_second)
        : -((-timestamp_ns + k_ns_per_second - 1) / k_ns_per_second);
    time_t t = static_cast<time_t>(whole_seconds);
    struct tm tm_buf;

#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif

    char buf[32];
    if (step_ns >= k_ns_per_minute) {
        std::strftime(buf, sizeof(buf), "%H:%M", &tm_buf);
    }
    else {
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
    }
    return buf;
}

} // namespace vnm::plot
