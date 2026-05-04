#pragma once

// VNM Plot Library - Plot Time Axis
// Shared time axis state for synchronized Plot_widget instances.
// This QObject lives on the GUI thread; Plot_widget mirrors values under locks.

#include <QObject>

#include <unordered_map>

namespace vnm::plot {

class Plot_time_axis : public QObject
{
    Q_OBJECT

    // Time properties are qint64 nanoseconds (API convention).
    Q_PROPERTY(qint64 t_min READ t_min WRITE set_t_min NOTIFY t_limits_changed)
    Q_PROPERTY(qint64 t_max READ t_max WRITE set_t_max NOTIFY t_limits_changed)
    Q_PROPERTY(qint64 t_available_min READ t_available_min WRITE set_t_available_min NOTIFY t_limits_changed)
    Q_PROPERTY(qint64 t_available_max READ t_available_max WRITE set_t_available_max NOTIFY t_limits_changed)
    Q_PROPERTY(bool sync_vbar_width READ sync_vbar_width WRITE set_sync_vbar_width NOTIFY sync_vbar_width_changed)
    Q_PROPERTY(bool indicator_active READ indicator_active NOTIFY indicator_state_changed)
    Q_PROPERTY(qint64 indicator_t READ indicator_t NOTIFY indicator_state_changed)
    Q_PROPERTY(bool indicator_x_norm_valid READ indicator_x_norm_valid NOTIFY indicator_state_changed)
    Q_PROPERTY(double indicator_x_norm READ indicator_x_norm NOTIFY indicator_state_changed)

public:
    explicit Plot_time_axis(QObject* parent = nullptr);

    qint64 t_min() const;
    qint64 t_max() const;
    qint64 t_available_min() const;
    qint64 t_available_max() const;

    void set_t_min(qint64 v);
    void set_t_max(qint64 v);
    void set_t_available_min(qint64 v);
    void set_t_available_max(qint64 v);

    bool sync_vbar_width() const;
    void set_sync_vbar_width(bool v);
    double shared_vbar_width_px() const;
    void update_shared_vbar_width(const QObject* owner, double width_px);
    void clear_shared_vbar_width(const QObject* owner);

    Q_INVOKABLE void set_t_range(qint64 t_min_ns, qint64 t_max_ns);
    Q_INVOKABLE void set_available_t_range(qint64 t_available_min_ns, qint64 t_available_max_ns);
    Q_INVOKABLE void adjust_t_from_mouse_diff(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_diff_on_preview(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos);
    Q_INVOKABLE void adjust_t_from_pivot_and_scale(double pivot, double scale);
    Q_INVOKABLE void adjust_t_to_target(qint64 target_min_ns, qint64 target_max_ns);
    Q_INVOKABLE void set_indicator_state(QObject* owner, bool active, qint64 t_ns);
    Q_INVOKABLE void set_indicator_state(QObject* owner, bool active, qint64 t_ns, double x_norm);
    Q_INVOKABLE bool indicator_owned_by(QObject* owner) const;

    bool indicator_active() const;
    qint64 indicator_t() const;
    bool indicator_x_norm_valid() const;
    double indicator_x_norm() const;

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

    qint64 m_t_min = 5000;
    qint64 m_t_max = 10000;
    qint64 m_t_available_min = 0;
    qint64 m_t_available_max = 10000;

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
