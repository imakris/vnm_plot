#pragma once

// VNM Plot Library - Plot Time Axis
// Shared time axis state for synchronized Plot_widget instances.
// This QObject lives on the GUI thread; Plot_widget mirrors values under locks.

#include <QObject>

namespace vnm::plot {

class Plot_time_axis : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double t_min READ t_min WRITE set_t_min NOTIFY t_limits_changed)
    Q_PROPERTY(double t_max READ t_max WRITE set_t_max NOTIFY t_limits_changed)
    Q_PROPERTY(double t_available_min READ t_available_min WRITE set_t_available_min NOTIFY t_limits_changed)
    Q_PROPERTY(double t_available_max READ t_available_max WRITE set_t_available_max NOTIFY t_limits_changed)

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

    Q_INVOKABLE void set_t_range(double t_min, double t_max);
    Q_INVOKABLE void set_available_t_range(double t_available_min, double t_available_max);
    Q_INVOKABLE void adjust_t_from_mouse_diff(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_diff_on_preview(double ref_width, double diff);
    Q_INVOKABLE void adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos);
    Q_INVOKABLE void adjust_t_from_pivot_and_scale(double pivot, double scale);
    Q_INVOKABLE void adjust_t_to_target(double target_min, double target_max);

signals:
    void t_limits_changed();

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
};

} // namespace vnm::plot
