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

    Q_PROPERTY(double t_min READ t_min WRITE set_t_min NOTIFY t_limits_changed)
    Q_PROPERTY(double t_max READ t_max WRITE set_t_max NOTIFY t_limits_changed)
    Q_PROPERTY(double t_available_min READ t_available_min WRITE set_t_available_min NOTIFY t_limits_changed)
    Q_PROPERTY(double t_available_max READ t_available_max WRITE set_t_available_max NOTIFY t_limits_changed)
    Q_PROPERTY(bool sync_vbar_width READ sync_vbar_width WRITE set_sync_vbar_width NOTIFY sync_vbar_width_changed)
    Q_PROPERTY(bool indicator_active READ indicator_active NOTIFY indicator_state_changed)
    Q_PROPERTY(double indicator_t READ indicator_t NOTIFY indicator_state_changed)
    Q_PROPERTY(bool indicator_x_norm_valid READ indicator_x_norm_valid NOTIFY indicator_state_changed)
    Q_PROPERTY(double indicator_x_norm READ indicator_x_norm NOTIFY indicator_state_changed)

public:
    explicit Plot_time_axis(QObject* parent = nullptr);

    double t_min() const;
    double t_max() const;
    double t_available_min() const;
    double t_available_max() const;

    void set_t_min(double v);
    void set_t_max(double v);
    void set_t_available_min(double v);
    void set_t_available_max(double v);

    bool sync_vbar_width() const;
    void set_sync_vbar_width(bool v);
    double shared_vbar_width_px() const;
    void update_shared_vbar_width(const QObject* owner, double width_px);
    void clear_shared_vbar_width(const QObject* owner);

    Q_INVOKABLE void set_t_range(double t_min, double t_max);
    Q_INVOKABLE void set_available_t_range(double t_available_min, double t_available_max);
    Q_INVOKABLE void adjust_t_from_mouse_diff(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_diff_on_preview(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos);
    Q_INVOKABLE void adjust_t_from_pivot_and_scale(double pivot, double scale);
    Q_INVOKABLE void adjust_t_to_target(double target_min, double target_max);
    Q_INVOKABLE void set_indicator_state(QObject* owner, bool active, double t);
    Q_INVOKABLE void set_indicator_state(QObject* owner, bool active, double t, double x_norm);
    Q_INVOKABLE bool indicator_owned_by(QObject* owner) const;

    bool indicator_active() const;
    double indicator_t() const;
    bool indicator_x_norm_valid() const;
    double indicator_x_norm() const;

signals:
    void t_limits_changed();
    void sync_vbar_width_changed();
    void shared_vbar_width_changed(double width_px);
    void indicator_state_changed();

private:
    bool set_limits_if_changed(
        double t_min,
        double t_max,
        double t_available_min,
        double t_available_max);

    double m_t_min = 5000.0;
    double m_t_max = 10000.0;
    double m_t_available_min = 0.0;
    double m_t_available_max = 10000.0;

    bool m_sync_vbar_width = false;
    std::unordered_map<const QObject*, double> m_vbar_width_by_owner;
    double m_shared_vbar_width_px = 0.0;

    QObject* m_indicator_owner = nullptr;
    bool m_indicator_active = false;
    double m_indicator_t = 0.0;
    bool m_indicator_x_norm_valid = false;
    double m_indicator_x_norm = 0.0;
};

} // namespace vnm::plot
