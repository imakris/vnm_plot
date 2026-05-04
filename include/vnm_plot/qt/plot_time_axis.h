#pragma once

// VNM Plot Library - Plot Time Axis
// Shared time axis state for synchronized Plot_widget instances.
// This QObject lives on the GUI thread; Plot_widget mirrors values under locks.
//
// Time-unit convention: matches Plot_widget. C++ stores int64 nanoseconds;
// QML-facing properties, signals, and Q_INVOKABLE timestamp parameters are
// expressed in milliseconds-since-epoch. See plot_widget.h for the
// rationale (JS double precision boundary at 2^53). Conversions happen
// in the Q_PROPERTY accessors and Q_INVOKABLE entry points via
// ns_to_ms_for_qml() / ms_for_qml_to_ns().

#include <vnm_plot/qt/plot_widget.h>

#include <QObject>

#include <limits>
#include <unordered_map>

namespace vnm::plot {

class Plot_time_axis : public QObject
{
    Q_OBJECT

    // QML-facing properties: milliseconds-since-epoch. Q_PROPERTY READ/WRITE
    // names end in _qml_ms to keep the unit obvious at the boundary.
    Q_PROPERTY(qint64 t_min READ t_min_qml_ms WRITE set_t_min_qml_ms NOTIFY t_limits_changed)
    Q_PROPERTY(qint64 t_max READ t_max_qml_ms WRITE set_t_max_qml_ms NOTIFY t_limits_changed)
    Q_PROPERTY(qint64 t_available_min READ t_available_min_qml_ms WRITE set_t_available_min_qml_ms NOTIFY t_limits_changed)
    Q_PROPERTY(qint64 t_available_max READ t_available_max_qml_ms WRITE set_t_available_max_qml_ms NOTIFY t_limits_changed)
    Q_PROPERTY(bool sync_vbar_width READ sync_vbar_width WRITE set_sync_vbar_width NOTIFY sync_vbar_width_changed)
    Q_PROPERTY(bool indicator_active READ indicator_active NOTIFY indicator_state_changed)
    Q_PROPERTY(qint64 indicator_t READ indicator_t_qml_ms NOTIFY indicator_state_changed)
    Q_PROPERTY(bool indicator_x_norm_valid READ indicator_x_norm_valid NOTIFY indicator_state_changed)
    Q_PROPERTY(double indicator_x_norm READ indicator_x_norm NOTIFY indicator_state_changed)

public:
    explicit Plot_time_axis(QObject* parent = nullptr);

    // C++ API: nanoseconds.
    qint64 t_min() const;
    qint64 t_max() const;
    qint64 t_available_min() const;
    qint64 t_available_max() const;

    void set_t_min(qint64 v);
    void set_t_max(qint64 v);
    void set_t_available_min(qint64 v);
    void set_t_available_max(qint64 v);

    // QML-facing accessors: milliseconds-since-epoch. Not part of the C++
    // API surface.
    qint64 t_min_qml_ms() const;
    qint64 t_max_qml_ms() const;
    qint64 t_available_min_qml_ms() const;
    qint64 t_available_max_qml_ms() const;

    void set_t_min_qml_ms(qint64 v_ms);
    void set_t_max_qml_ms(qint64 v_ms);
    void set_t_available_min_qml_ms(qint64 v_ms);
    void set_t_available_max_qml_ms(qint64 v_ms);

    bool sync_vbar_width() const;
    void set_sync_vbar_width(bool v);
    double shared_vbar_width_px() const;
    void update_shared_vbar_width(const QObject* owner, double width_px);
    void clear_shared_vbar_width(const QObject* owner);

    // C++-facing methods (timestamp arguments are int64 nanoseconds).
    // These are NOT Q_INVOKABLE because the only callers live in
    // plot_widget.cpp; exposing them to QML in nanoseconds would tear
    // their qint64 values via JS double precision loss.
    void set_t_range(qint64 t_min_ns, qint64 t_max_ns);
    void set_available_t_range(qint64 t_available_min_ns, qint64 t_available_max_ns);
    void adjust_t_to_target(qint64 target_min_ns, qint64 target_max_ns);

    Q_INVOKABLE void adjust_t_from_mouse_diff(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_diff_on_preview(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos);
    Q_INVOKABLE void adjust_t_from_pivot_and_scale(double pivot, double scale);
    // QML-facing indicator API: timestamp is milliseconds-since-epoch.
    Q_INVOKABLE void set_indicator_state(QObject* owner, bool active, qint64 t_ms);
    Q_INVOKABLE void set_indicator_state(QObject* owner, bool active, qint64 t_ms, double x_norm);
    Q_INVOKABLE bool indicator_owned_by(QObject* owner) const;

    bool indicator_active() const;
    qint64 indicator_t() const;
    qint64 indicator_t_qml_ms() const;
    bool indicator_x_norm_valid() const;
    double indicator_x_norm() const;

    // Sentinel that marks a t_* member as "not yet set". A freshly-constructed
    // axis carries this in every t_* slot so callers (notably
    // Plot_widget::sync_time_axis_state) can distinguish "no range configured"
    // from "user set this to INT64_MIN" and avoid overwriting a widget's
    // explicitly-set view with stale defaults at attach time.
    static constexpr qint64 k_t_unset = std::numeric_limits<qint64>::min();

    bool view_initialized() const;
    bool available_initialized() const;

signals:
    void t_limits_changed();
    void sync_vbar_width_changed();
    void shared_vbar_width_changed(double width_px);
    void indicator_state_changed();

private:
    bool set_limits_if_changed(
        qint64 t_min_ns,
        qint64 t_max_ns,
        qint64 t_available_min_ns,
        qint64 t_available_max_ns);

    // Sentinel-initialized: see k_t_unset. The previous concrete defaults
    // (5000 / 10000) were carried over from a pre-int64 era when the unit
    // was seconds; reused as nanoseconds they collapsed every freshly-attached
    // widget's view to a 5-microsecond window via sync_time_axis_state.
    qint64 m_t_min = k_t_unset;
    qint64 m_t_max = k_t_unset;
    qint64 m_t_available_min = k_t_unset;
    qint64 m_t_available_max = k_t_unset;

    bool m_sync_vbar_width = false;
    std::unordered_map<const QObject*, double> m_vbar_width_by_owner;
    double m_shared_vbar_width_px = 0.0;

    QObject* m_indicator_owner = nullptr;
    bool m_indicator_active = false;
    qint64 m_indicator_t = 0;
    bool m_indicator_x_norm_valid = false;
    double m_indicator_x_norm = 0.0;
};

} // namespace vnm::plot
