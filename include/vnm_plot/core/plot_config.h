#pragma once

// VNM Plot Library - Configuration
// Injectable configuration for application-specific behavior.
// This allows vnm_plot to work without Lumis dependencies while
// still being customizable by the host application.

#include <ctime>
#include <functional>
#include <memory>
#include <string>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Auto V-Range Mode
// -----------------------------------------------------------------------------
// Controls how the auto v-range is computed when v_auto is enabled.
enum class Auto_v_range_mode
{
    // Default: match Lumis behavior (global full-resolution range).
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

    // --- Timestamp Formatting ---
    // Callback to format timestamps for axis labels.
    // Parameters: timestamp (unix seconds), visible_range (seconds shown)
    // Returns: formatted string for display
    // If null, a default formatter is used.
    std::function<std::string(double timestamp, double visible_range)> format_timestamp;

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
    // Area fill alpha multiplier (0..1).
    double area_fill_alpha = 0.3;

    // --- Auto V-Range ---
    // Default is GLOBAL to preserve Lumis-style behavior.
    Auto_v_range_mode auto_v_range_mode = Auto_v_range_mode::GLOBAL;
    // Extra scale applied to auto v-range (0 = no padding).
    double auto_v_range_extra_scale = 0.0;

    // Default configuration
    static Plot_config make_default()
    {
        Plot_config cfg;
        cfg.dark_mode = false;
        cfg.show_text = true;
        cfg.font_size_px = 10.0;
        cfg.base_label_height_px = 14.0;
        cfg.auto_v_range_mode = Auto_v_range_mode::GLOBAL;
        cfg.clear_to_transparent = false;
        cfg.snap_lines_to_pixels = false;
        cfg.line_width_px = 1.0;
        cfg.area_fill_alpha = 0.3;
        cfg.auto_v_range_extra_scale = 0.0;
        return cfg;
    }
};

// -----------------------------------------------------------------------------
// Default Timestamp Formatter
// -----------------------------------------------------------------------------
// Simple formatter when no custom one is provided.
// For full formatting with timezone support, applications should provide
// their own formatter via Plot_config::format_timestamp.
inline std::string default_format_timestamp(double timestamp, double /*visible_range*/)
{
    // Simple ISO-style formatting
    // Applications should override for timezone-aware formatting
    time_t t = static_cast<time_t>(timestamp);
    struct tm tm_buf;

#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
    return buf;
}

} // namespace vnm::plot
