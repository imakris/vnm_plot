#pragma once

// VNM Plot Library - Plot Widget
// QQuickFramebufferObject-based plot widget for Qt Quick.

#include <vnm_plot/core/types.h>
#include <vnm_plot/core/plot_config.h>

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QQuickFramebufferObject>

#include <QVariantList>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace vnm::plot {

class Plot_renderer;
class Plot_time_axis;

// -----------------------------------------------------------------------------
// Plot_view
// -----------------------------------------------------------------------------
struct Plot_view
{
    std::optional<std::pair<double, double>> t_range;
    std::optional<std::pair<double, double>> t_available_range;
    std::optional<std::pair<float, float>> v_range;
    std::optional<bool> v_auto;
};

// -----------------------------------------------------------------------------
// Plot Widget
// -----------------------------------------------------------------------------
// Qt Quick widget for rendering GPU-accelerated plots.
// This is the main public interface for the vnm_plot library.
class Plot_widget : public QQuickFramebufferObject
{
    Q_OBJECT

    Q_PROPERTY(double t_min READ t_min NOTIFY t_limits_changed)
    Q_PROPERTY(double t_max READ t_max NOTIFY t_limits_changed)
    Q_PROPERTY(double t_available_min READ t_available_min NOTIFY t_limits_changed)
    Q_PROPERTY(double t_available_max READ t_available_max NOTIFY t_limits_changed)
    Q_PROPERTY(float v_min READ v_min NOTIFY v_limits_changed)
    Q_PROPERTY(float v_max READ v_max NOTIFY v_limits_changed)
    Q_PROPERTY(bool v_auto READ v_auto WRITE set_v_auto NOTIFY v_auto_changed)
    Q_PROPERTY(double preview_height READ preview_height WRITE set_preview_height NOTIFY preview_height_changed)
    Q_PROPERTY(double preview_height_target READ preview_height_target NOTIFY preview_height_target_changed)
    Q_PROPERTY(double preview_height_collapsed READ preview_height_collapsed CONSTANT)
    Q_PROPERTY(double reserved_height READ reserved_height NOTIFY preview_height_changed)
    Q_PROPERTY(double scaling_factor READ scaling_factor NOTIFY scaling_factor_changed)
    Q_PROPERTY(bool dark_mode READ dark_mode WRITE set_dark_mode NOTIFY dark_mode_changed)
    Q_PROPERTY(double grid_visibility READ grid_visibility WRITE set_grid_visibility NOTIFY grid_visibility_changed)
    Q_PROPERTY(double preview_visibility READ preview_visibility WRITE set_preview_visibility NOTIFY preview_visibility_changed)
    Q_PROPERTY(double line_width_px READ line_width_px WRITE set_line_width_px NOTIFY line_width_px_changed)
    Q_PROPERTY(double vbar_width_px READ vbar_width_pixels NOTIFY vbar_width_changed)
    Q_PROPERTY(double vbar_width_qml READ vbar_width_qml NOTIFY vbar_width_changed)
    Q_PROPERTY(Plot_time_axis* timeAxis READ time_axis WRITE set_time_axis NOTIFY time_axis_changed)

public:
    Plot_widget();
    ~Plot_widget() override;

    // --- Data Management ---

    // Add or update a data series
    void add_series(int id, std::shared_ptr<series_data_t> series);
    void apply_series_updates(const std::vector<std::pair<int, std::shared_ptr<series_data_t>>>& updates);

    // Remove a data series
    void remove_series(int id);

    // Clear all series
    void clear();

    // Get a snapshot of all series data
    std::map<int, std::shared_ptr<const series_data_t>> get_series_snapshot() const;

    // --- Configuration ---

    // Set the plot configuration
    // Note: grid_visibility, preview_visibility, and line_width_px are controlled by QML
    // properties and preserved across set_config() calls.
    void set_config(const Plot_config& config);
    Plot_config config() const;
    // Reset renderer-side view state (e.g., skip auto-range smoothing once).
    Q_INVOKABLE void reset_view_state();

    // Dark mode
    bool dark_mode() const;
    void set_dark_mode(bool dark);

    // Grid visibility (0..1 alpha; 0 = hidden, 1 = fully visible)
    double grid_visibility() const;
    void set_grid_visibility(double visibility);

    // Preview visibility (0..1 alpha; 0 = hidden, 1 = fully visible)
    double preview_visibility() const;
    void set_preview_visibility(double visibility);

    // Line width in pixels
    double line_width_px() const;
    void set_line_width_px(double width);

    // --- Time Range ---

    double t_min() const;
    double t_max() const;
    double t_available_min() const;
    double t_available_max() const;
    void set_t_range(double t_min, double t_max);
    void set_available_t_range(double t_min, double t_max);
    void set_view(const Plot_view& view);
    // Optional shared time axis (non-owning). When set, this widget mirrors its values.
    Plot_time_axis* time_axis() const;
    void set_time_axis(Plot_time_axis* axis);
    // Attach to another widget's time axis (no-op if missing).
    Q_INVOKABLE void attach_time_axis(Plot_widget* other);

    // --- Value Range ---

    float v_min() const;
    float v_max() const;
    bool v_auto() const;
    void set_v_auto(bool auto_scale);
    void set_v_range(float v_min, float v_max);

    // --- Preview ---

    double preview_height() const;
    void set_preview_height(double height);
    double preview_height_target() const;
    double preview_height_collapsed() const;
    double reserved_height() const;
    double scaling_factor() const;
    double vbar_width_pixels() const;
    double vbar_width_qml() const;
    Q_INVOKABLE void set_vbar_width(double vbar_width);
    Q_INVOKABLE void set_vbar_width_from_renderer(double px);
    // Render-thread updates to keep v_min/v_max in sync with auto range.
    Q_INVOKABLE void set_auto_v_range_from_renderer(float v_min, float v_max);

    // --- Interaction ---

    Q_INVOKABLE void adjust_t_from_mouse_diff(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_diff_on_preview(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos);
    Q_INVOKABLE void adjust_t_from_pivot_and_scale(double pivot, double scale);
    Q_INVOKABLE void adjust_v_from_mouse_diff(float ref_height, float diff);
    Q_INVOKABLE void adjust_v_from_pivot_and_scale(float pivot, float scale);
    Q_INVOKABLE void adjust_v_to_target(float target_vmin, float target_vmax);
    Q_INVOKABLE void auto_adjust_view(bool adjust_t, double extra_v_scale);
    Q_INVOKABLE void auto_adjust_view(bool adjust_t, double extra_v_scale, bool anchor_zero);
    Q_INVOKABLE bool can_zoom_in() const;
    Q_INVOKABLE double update_dpi_scaling_factor();
    Q_INVOKABLE void set_info_visible(bool v);
    Q_INVOKABLE void set_relative_preview_height(float relative);
    Q_INVOKABLE void set_preview_height_min(double v);
    Q_INVOKABLE void set_preview_height_max(double v);
    Q_INVOKABLE void set_show_if_calculated_preview_height_below_min(bool v);
    Q_INVOKABLE void set_preview_height_steps(int steps);

    Q_INVOKABLE QVariantList get_indicator_samples(double x, double plot_width, double plot_height) const;

    // --- Qt Quick FBO Interface ---

    Renderer* createRenderer() const override;

signals:
    void t_limits_changed();
    void v_limits_changed();
    void v_auto_changed();
    void preview_height_changed();
    void preview_height_target_changed(double target);
    void scaling_factor_changed();
    void dark_mode_changed();
    void grid_visibility_changed();
    void preview_visibility_changed();
    void line_width_px_changed();
    void vbar_width_changed();
    void time_axis_changed();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void timerEvent(QTimerEvent* ev) override;
    void adjust_t_to_target(double target_tmin, double target_tmax);
    std::pair<float, float> manual_v_range() const;
    bool rendered_v_range(float& out_min, float& out_max) const;

private:
    friend class Plot_renderer;

    // Lock order (if ever needed concurrently): config -> data_cfg -> series.
    // Prefer holding only one lock at a time.

    // Configuration
    Plot_config m_config;
    mutable std::shared_mutex m_config_mutex;

    // Data configuration
    data_config_t m_data_cfg;
    mutable std::shared_mutex m_data_cfg_mutex;

    // Series data
    std::map<int, std::shared_ptr<const series_data_t>> m_series;
    mutable std::shared_mutex m_series_mutex;

    // UI state
    std::atomic<bool> m_visible{false};
    std::atomic<bool> m_v_auto{true};
    std::atomic<bool> m_show_info{true};
    std::atomic<bool> m_view_state_reset_requested{false};
    mutable std::atomic<float> m_rendered_v_min{0.0f};
    mutable std::atomic<float> m_rendered_v_max{1.0f};
    mutable std::atomic<bool> m_rendered_v_range_valid{false};
    std::atomic<double> m_vbar_width_px{0.0};
    QBasicTimer m_vbar_width_timer;
    QElapsedTimer m_vbar_width_anim_elapsed;
    double m_vbar_width_anim_start_px = 0.0;
    double m_vbar_width_anim_target_px = 0.0;

    double m_preview_height = 0.0;
    double m_preview_height_target = 0.0;
    double m_adjusted_preview_height = 0.0;
    bool m_preview_height_initialized = false;
    float m_relative_preview_height = 0.0f;
    double m_preview_height_min = 0.0;
    double m_preview_height_max = 0.0;
    bool m_show_if_calculated_preview_height_below_min = true;
    int m_preview_height_steps = 0;

    double m_adjusted_font_size = 12.0;
    double m_base_label_height = 14.0;
    double m_scaling_factor = 1.0;

    void recalculate_preview_height();
    double compute_preview_height_px(double widget_height_px) const;
    std::pair<float, float> current_v_range() const;
    data_config_t data_cfg_snapshot() const;

    bool consume_view_state_reset_request();
    void set_rendered_v_range(float v_min, float v_max) const;
    void sync_time_axis_state();
    void clear_time_axis();
    void apply_vbar_width_target(double px);

    QPointer<Plot_time_axis> m_time_axis;
    QMetaObject::Connection m_time_axis_connection;
    QMetaObject::Connection m_time_axis_destroyed_connection;
    QMetaObject::Connection m_time_axis_vbar_connection;
};

} // namespace vnm::plot
