#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/qt/plot_renderer.h>
#include <vnm_plot/qt/plot_time_axis.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/algo.h>

#include <QGuiApplication>
#include <QDebug>
#include <QQuickWindow>
#include <QScreen>

#include <QColor>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Forward declare the Qt-generated resource init function (at global scope)
int qInitResources_vnm_plot();

// Call resource init at global scope before any namespace
inline void vnm_plot_init_qt_resources()
{
    static bool done = false;
    if (!done) {
        qInitResources_vnm_plot();
        done = true;
    }
}

namespace {

// Get DPI scaling factor for a specific window
double dpi_scaling_for_window([[maybe_unused]] void* native_handle)
{
#ifdef _WIN32
    // Use the provided window handle, or fall back to primary screen
    HWND hwnd = static_cast<HWND>(native_handle);
    if (hwnd) {
        return GetDpiForWindow(hwnd) / 96.0;
    }
    // Fallback: use system DPI
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        return dpi / 96.0;
    }
    return 1.0;
#else
    const auto* const screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return 1.0;
    }
    return screen->logicalDotsPerInch() / 96.0;
#endif
}

} // anonymous namespace

namespace vnm::plot {
using namespace detail;

Plot_widget::Plot_widget()
    : QQuickFramebufferObject()
{
    vnm_plot_init_qt_resources();

    m_relative_preview_height = 0.3f;
    m_preview_height_min = 30.0;
    m_preview_height_max = 150.0;
    m_show_if_calculated_preview_height_below_min = false;
    m_preview_height_steps = 2;

    update_dpi_scaling_factor();

    setMirrorVertically(true);
    setFlag(ItemHasContents, true);
}

Plot_widget::~Plot_widget() = default;

void Plot_widget::add_series(int id, std::shared_ptr<series_data_t> series)
{
    std::unique_lock lock(m_series_mutex);
    m_series[id] = std::move(series);
    update();
}

void Plot_widget::remove_series(int id)
{
    std::unique_lock lock(m_series_mutex);
    m_series.erase(id);
    update();
}

void Plot_widget::clear()
{
    std::unique_lock lock(m_series_mutex);
    m_series.clear();
    update();
}

std::map<int, std::shared_ptr<series_data_t>> Plot_widget::get_series_snapshot() const
{
    std::shared_lock lock(m_series_mutex);
    return m_series;
}

void Plot_widget::set_config(const Plot_config& config)
{
    {
        std::unique_lock lock(m_config_mutex);
        const double prev_grid_visibility = m_config.grid_visibility;
        const double prev_preview_visibility = m_config.preview_visibility;
        const double prev_line_width_px = m_config.line_width_px;
        m_config = config;
        m_config.grid_visibility = prev_grid_visibility;      // Preserve QML-controlled setting
        m_config.preview_visibility = prev_preview_visibility; // Preserve QML-controlled setting
        m_config.line_width_px = prev_line_width_px;          // Preserve QML-controlled setting
    }
    m_adjusted_font_size = m_config.font_size_px * m_scaling_factor;
    m_base_label_height = m_config.base_label_height_px * m_scaling_factor;
    if (m_config.preview_height_px > 0.0) {
        set_preview_height(m_config.preview_height_px);
    }
    else {
        recalculate_preview_height();
    }
    update();
}

Plot_config Plot_widget::config() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config;
}

void Plot_widget::reset_view_state()
{
    m_view_state_reset_requested.store(true, std::memory_order_release);
    m_rendered_v_range_valid.store(false, std::memory_order_release);
    update();
}

bool Plot_widget::dark_mode() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config.dark_mode;
}

void Plot_widget::set_dark_mode(bool dark)
{
    {
        std::unique_lock lock(m_config_mutex);
        if (m_config.dark_mode == dark) {
            return;
        }
        m_config.dark_mode = dark;
    }
    emit dark_mode_changed();
    update();
}

double Plot_widget::grid_visibility() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config.grid_visibility;
}

void Plot_widget::set_grid_visibility(double visibility)
{
    // Clamp to 0..1
    visibility = std::clamp(visibility, 0.0, 1.0);
    {
        std::unique_lock lock(m_config_mutex);
        if (m_config.grid_visibility == visibility) {
            return;
        }
        m_config.grid_visibility = visibility;
    }
    emit grid_visibility_changed();
    update();
}

double Plot_widget::preview_visibility() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config.preview_visibility;
}

void Plot_widget::set_preview_visibility(double visibility)
{
    // Clamp to 0..1
    visibility = std::clamp(visibility, 0.0, 1.0);
    {
        std::unique_lock lock(m_config_mutex);
        if (m_config.preview_visibility == visibility) {
            return;
        }
        m_config.preview_visibility = visibility;
    }
    emit preview_visibility_changed();
    update();
}

double Plot_widget::line_width_px() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config.line_width_px;
}

void Plot_widget::set_line_width_px(double width)
{
    {
        std::unique_lock lock(m_config_mutex);
        if (m_config.line_width_px == width) {
            return;
        }
        m_config.line_width_px = width;
    }
    emit line_width_px_changed();
    update();
}

double Plot_widget::t_min() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.t_min;
}

double Plot_widget::t_max() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.t_max;
}

double Plot_widget::t_available_min() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.t_available_min;
}

double Plot_widget::t_available_max() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.t_available_max;
}

void Plot_widget::set_t_range(double t_min, double t_max)
{
    if (m_time_axis) {
        m_time_axis->set_t_range(t_min, t_max);
        return;
    }
    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.t_min = t_min;
        m_data_cfg.t_max = t_max;
    }
    emit t_limits_changed();
    update();
}

void Plot_widget::set_available_t_range(double t_min, double t_max)
{
    if (m_time_axis) {
        m_time_axis->set_available_t_range(t_min, t_max);
        return;
    }
    {
        std::unique_lock lock(m_data_cfg_mutex);
        if (t_max > t_min) {
            const double span = t_max - t_min;
            const double cur_span = m_data_cfg.t_max - m_data_cfg.t_min;
            if (cur_span > span) {
                m_data_cfg.t_min = t_min;
                m_data_cfg.t_max = t_max;
            }
            else {
                if (m_data_cfg.t_min < t_min) {
                    m_data_cfg.t_min = t_min;
                    m_data_cfg.t_max = t_min + cur_span;
                }
                if (m_data_cfg.t_max > t_max) {
                    m_data_cfg.t_max = t_max;
                    m_data_cfg.t_min = t_max - cur_span;
                }
            }
        }
        m_data_cfg.t_available_min = t_min;
        m_data_cfg.t_available_max = t_max;
    }
    emit t_limits_changed();
    update();
}

Plot_time_axis* Plot_widget::time_axis() const
{
    return m_time_axis.data();
}

void Plot_widget::set_time_axis(Plot_time_axis* axis)
{
    if (m_time_axis == axis) {
        return;
    }

    if (m_time_axis) {
        if (m_time_axis->sync_vbar_width()) {
            m_time_axis->clear_shared_vbar_width(this);
        }
        QObject::disconnect(m_time_axis, nullptr, this, nullptr);
        m_time_axis_connection = {};
        m_time_axis_destroyed_connection = {};
        m_time_axis_vbar_connection = {};
    }

    m_time_axis = axis;

    if (m_time_axis) {
        m_time_axis_connection = QObject::connect(
            m_time_axis,
            &Plot_time_axis::t_limits_changed,
            this,
            [this]() { sync_time_axis_state(); });
        m_time_axis_destroyed_connection = QObject::connect(
            m_time_axis,
            &QObject::destroyed,
            this,
            [this]() { clear_time_axis(); });
        m_time_axis_vbar_connection = QObject::connect(
            m_time_axis,
            &Plot_time_axis::shared_vbar_width_changed,
            this,
            [this](double px) {
                if (!m_time_axis || !m_time_axis->sync_vbar_width()) {
                    return;
                }
                if (px <= 0.0 || !std::isfinite(px)) {
                    return;
                }
                apply_vbar_width_target(px);
            });
        sync_time_axis_state();
        if (m_time_axis->sync_vbar_width()) {
            const double current_px = m_vbar_width_px.load(std::memory_order_acquire);
            if (std::isfinite(current_px) && current_px > 0.0) {
                m_time_axis->update_shared_vbar_width(this, current_px);
            }
            const double shared_px = m_time_axis->shared_vbar_width_px();
            if (std::isfinite(shared_px) && shared_px > 0.0) {
                apply_vbar_width_target(shared_px);
            }
        }
    }

    emit time_axis_changed();
    update();
}

void Plot_widget::attach_time_axis(Plot_widget* other)
{
    if (!other) {
        qWarning() << "vnm_plot: attach_time_axis called with null widget.";
        return;
    }
    if (!other->time_axis()) {
        qWarning() << "vnm_plot: attach_time_axis called but other widget has no time axis.";
        return;
    }
    set_time_axis(other->time_axis());
}

float Plot_widget::v_min() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    if (m_v_auto.load(std::memory_order_acquire)) {
        return m_data_cfg.v_min;
    }
    return m_data_cfg.v_manual_min;
}

float Plot_widget::v_max() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    if (m_v_auto.load(std::memory_order_acquire)) {
        return m_data_cfg.v_max;
    }
    return m_data_cfg.v_manual_max;
}

bool Plot_widget::v_auto() const
{
    return m_v_auto.load(std::memory_order_acquire);
}

void Plot_widget::set_v_auto(bool auto_scale)
{
    if (m_v_auto.exchange(auto_scale, std::memory_order_acq_rel) != auto_scale) {
        emit v_auto_changed();
        update();
    }
}

void Plot_widget::set_v_range(float v_min, float v_max)
{
    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.v_min = v_min;
        m_data_cfg.v_max = v_max;
        m_data_cfg.v_manual_min = v_min;
        m_data_cfg.v_manual_max = v_max;
    }
    emit v_limits_changed();
    update();
}

double Plot_widget::preview_height() const
{
    return m_preview_height;
}

void Plot_widget::set_preview_height(double height)
{
    if (std::abs(m_preview_height - height) > 0.001) {
        m_preview_height = height;
        m_adjusted_preview_height = height * m_scaling_factor;
        emit preview_height_changed();
        update();
    }
}

double Plot_widget::preview_height_target() const
{
    return m_preview_height_target;
}

double Plot_widget::preview_height_collapsed() const
{
    return m_preview_height_min;
}

double Plot_widget::reserved_height() const
{
    return (m_scaling_factor > 0.0)
        ? (m_base_label_height / m_scaling_factor + m_preview_height)
        : m_preview_height;
}

double Plot_widget::scaling_factor() const
{
    return m_scaling_factor;
}

double Plot_widget::vbar_width_pixels() const
{
    return m_vbar_width_px.load(std::memory_order_acquire);
}

double Plot_widget::vbar_width_qml() const
{
    return (m_scaling_factor > 0.0)
        ? m_vbar_width_px.load(std::memory_order_acquire) / m_scaling_factor
        : 0.0;
}

void Plot_widget::set_vbar_width(double vbar_width)
{
    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.vbar_width = vbar_width;
    }

    const double px = vbar_width * m_scaling_factor;
    m_vbar_width_px.store(px, std::memory_order_release);
    emit vbar_width_changed();
    update();

    if (m_time_axis && m_time_axis->sync_vbar_width()) {
        m_time_axis->update_shared_vbar_width(this, px);
    }
}

void Plot_widget::apply_vbar_width_target(double target)
{
    const double current = m_vbar_width_px.load(std::memory_order_acquire);

    if (!std::isfinite(current) || current <= 0.0) {
        m_vbar_width_px.store(target, std::memory_order_release);
        emit vbar_width_changed();
        update();
        return;
    }

    if (std::abs(target - current) <= k_vbar_width_change_threshold_d &&
        !m_vbar_width_timer.isActive())
    {
        return;
    }

    if (m_vbar_width_timer.isActive() &&
        std::abs(target - m_vbar_width_anim_target_px) <= 1e-6)
    {
        return;
    }

    m_vbar_width_anim_start_px = current;
    m_vbar_width_anim_target_px = target;
    m_vbar_width_anim_elapsed.restart();

    if (!m_vbar_width_timer.isActive()) {
        m_vbar_width_timer.start(16, this);
    }
}

void Plot_widget::set_vbar_width_from_renderer(double px)
{
    if (m_time_axis && m_time_axis->sync_vbar_width()) {
        m_time_axis->update_shared_vbar_width(this, px);
        return;
    }

    apply_vbar_width_target(px);
}

void Plot_widget::set_auto_v_range_from_renderer(float v_min, float v_max)
{
    if (!std::isfinite(v_min) || !std::isfinite(v_max)) {
        return;
    }

    if (!m_v_auto.load(std::memory_order_acquire)) {
        return;
    }

    constexpr float k_auto_v_eps = 1e-6f;
    bool changed = false;
    {
        std::unique_lock lock(m_data_cfg_mutex);
        if (std::abs(m_data_cfg.v_min - v_min) > k_auto_v_eps ||
            std::abs(m_data_cfg.v_max - v_max) > k_auto_v_eps)
        {
            m_data_cfg.v_min = v_min;
            m_data_cfg.v_max = v_max;
            changed = true;
        }
    }

    if (changed) {
        emit v_limits_changed();
        update();
    }
}

double Plot_widget::update_dpi_scaling_factor()
{
    // Get the native window handle for accurate DPI on multi-monitor setups
    void* native_handle = nullptr;
#ifdef _WIN32
    if (auto* qwin = window()) {
        native_handle = reinterpret_cast<void*>(qwin->winId());
    }
#endif
    const double scaling = dpi_scaling_for_window(native_handle);
    if (std::abs(m_scaling_factor - scaling) > 1e-6) {
        m_scaling_factor = scaling;
        emit scaling_factor_changed();
    }

    Plot_config cfg;
    {
        std::shared_lock lock(m_config_mutex);
        cfg = m_config;
    }

    m_adjusted_font_size = cfg.font_size_px * m_scaling_factor;
    m_base_label_height = cfg.base_label_height_px * m_scaling_factor;
    if (cfg.preview_height_px > 0.0) {
        set_preview_height(cfg.preview_height_px);
    }
    else {
        recalculate_preview_height();
    }
    update();
    return scaling;
}

void Plot_widget::set_info_visible(bool v)
{
    const bool prev = m_show_info.exchange(v, std::memory_order_acq_rel);
    if (prev != v) {
        update();
    }
}

void Plot_widget::set_relative_preview_height(float relative)
{
    const float clamped = std::clamp(relative, 0.0f, 1.0f);
    if (m_relative_preview_height != clamped) {
        m_relative_preview_height = clamped;
        recalculate_preview_height();
    }
}

void Plot_widget::set_preview_height_min(double v)
{
    if (v < 0.0) {
        v = 0.0;
    }
    if (m_preview_height_min != v) {
        m_preview_height_min = v;
        if (m_preview_height_max < m_preview_height_min) {
            m_preview_height_max = m_preview_height_min;
        }
        recalculate_preview_height();
    }
}

void Plot_widget::set_preview_height_max(double v)
{
    if (v < 0.0) {
        v = 0.0;
    }
    if (m_preview_height_max != v) {
        m_preview_height_max = v;
        if (m_preview_height_max < m_preview_height_min) {
            m_preview_height_min = m_preview_height_max;
        }
        recalculate_preview_height();
    }
}

void Plot_widget::set_show_if_calculated_preview_height_below_min(bool v)
{
    if (m_show_if_calculated_preview_height_below_min != v) {
        m_show_if_calculated_preview_height_below_min = v;
        recalculate_preview_height();
    }
}

void Plot_widget::set_preview_height_steps(int steps)
{
    if (steps < 0) {
        steps = 0;
    }
    if (m_preview_height_steps != steps) {
        m_preview_height_steps = steps;
        recalculate_preview_height();
    }
}

void Plot_widget::adjust_t_from_mouse_diff(double ref_width, double diff)
{
    if (m_time_axis) {
        m_time_axis->adjust_t_from_mouse_diff(ref_width, diff);
        return;
    }
    if (ref_width <= 0.0) {
        return;
    }

    const auto cfg = data_cfg_snapshot();
    const double t_min_val = cfg.t_min;
    const double t_max_val = cfg.t_max;

    const double span = t_max_val - t_min_val;
    const double delta = diff * span / ref_width;
    adjust_t_to_target(t_min_val - delta, t_max_val - delta);
}

void Plot_widget::adjust_t_from_mouse_diff_on_preview(double ref_width, double diff)
{
    if (m_time_axis) {
        m_time_axis->adjust_t_from_mouse_diff_on_preview(ref_width, diff);
        return;
    }
    if (ref_width <= 0.0) {
        return;
    }

    const auto cfg = data_cfg_snapshot();
    const double t_min_val = cfg.t_min;
    const double t_max_val = cfg.t_max;
    const double avail_min = cfg.t_available_min;
    const double avail_max = cfg.t_available_max;

    const double avail_span = avail_max - avail_min;
    const double delta = diff * avail_span / ref_width;
    adjust_t_to_target(t_min_val + delta, t_max_val + delta);
}

void Plot_widget::adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos)
{
    if (m_time_axis) {
        m_time_axis->adjust_t_from_mouse_pos_on_preview(ref_width, x_pos);
        return;
    }
    if (ref_width <= 0.0) {
        return;
    }

    const auto cfg = data_cfg_snapshot();
    const double t_min_val = cfg.t_min;
    const double t_max_val = cfg.t_max;
    const double avail_min = cfg.t_available_min;
    const double avail_max = cfg.t_available_max;

    const double span = t_max_val - t_min_val;
    const double avail_span = avail_max - avail_min;
    const double rel = x_pos / ref_width;
    const double new_center = avail_min + rel * avail_span;
    adjust_t_to_target(new_center - span * 0.5, new_center + span * 0.5);
}

void Plot_widget::adjust_t_from_pivot_and_scale(double pivot, double scale)
{
    if (m_time_axis) {
        m_time_axis->adjust_t_from_pivot_and_scale(pivot, scale);
        return;
    }
    if (scale <= 0.0) {
        return;
    }

    const auto cfg = data_cfg_snapshot();
    const double t_min_val = cfg.t_min;
    const double t_max_val = cfg.t_max;

    const double t_pivot = t_min_val + (t_max_val - t_min_val) * pivot;
    const double new_min = t_pivot - (t_pivot - t_min_val) * scale;
    const double new_max = t_pivot + (t_max_val - t_pivot) * scale;
    adjust_t_to_target(new_min, new_max);
}

void Plot_widget::adjust_v_from_mouse_diff(float ref_height, float diff)
{
    if (ref_height <= 0.0f) {
        return;
    }

    const auto [vmin, vmax] = current_v_range();
    const float span = vmax - vmin;
    const float delta = diff * span / ref_height;
    adjust_v_to_target(vmin + delta, vmax + delta);
}

void Plot_widget::adjust_v_from_pivot_and_scale(float pivot, float scale)
{
    if (scale <= 0.0f) {
        return;
    }

    const auto [vmin, vmax] = current_v_range();
    const float v_pivot = vmin + (vmax - vmin) * (1.0f - pivot);
    const float v0 = v_pivot - (v_pivot - vmin) * scale;
    const float v1 = v_pivot + (vmax - v_pivot) * scale;
    adjust_v_to_target(v0, v1);
}

void Plot_widget::adjust_v_to_target(float target_vmin, float target_vmax)
{
    const float min_span = min_v_span_for(target_vmin, target_vmax);
    if (target_vmax - target_vmin < min_span) {
        const float mid = 0.5f * (target_vmax + target_vmin);
        target_vmin = mid - 0.5f * min_span;
        target_vmax = mid + 0.5f * min_span;
    }

    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.v_manual_min = target_vmin;
        m_data_cfg.v_manual_max = target_vmax;
        m_data_cfg.v_min = target_vmin;
        m_data_cfg.v_max = target_vmax;
    }

    set_v_auto(false);
    emit v_limits_changed();
    update();
}

void Plot_widget::auto_adjust_view(bool adjust_t, double extra_v_scale)
{
    auto_adjust_view(adjust_t, extra_v_scale, true);
}

void Plot_widget::auto_adjust_view(bool adjust_t, double extra_v_scale, bool anchor_zero)
{
    const auto cfg = data_cfg_snapshot();
    const double window_tmin = cfg.t_min;
    const double window_tmax = cfg.t_max;

    if (!(window_tmax > window_tmin)) {
        return;
    }

    struct aggregated_range_t
    {
        double vmin;
        double vmax;
        double tmin;
        double tmax;
    };

    bool have_any = false;
    aggregated_range_t agg{};

    const auto include_sample = [&](double ts, double low, double high) {
        if (!have_any) {
            agg = {low, high, ts, ts};
            have_any = true;
            return;
        }
        agg.vmin = std::min(agg.vmin, low);
        agg.vmax = std::max(agg.vmax, high);
        agg.tmin = std::min(agg.tmin, ts);
        agg.tmax = std::max(agg.tmax, ts);
    };

    std::vector<std::shared_ptr<series_data_t>> sources;
    {
        std::shared_lock lock(m_series_mutex);
        sources.reserve(m_series.size());
        for (const auto& [_, series] : m_series) {
            if (series && series->enabled) {
                sources.push_back(series);
            }
        }
    }

    for (const auto& series : sources) {
        if (!series || !series->data_source || !series->access.get_timestamp) {
            continue;
        }

        auto snapshot = series->data_source->snapshot(0);
        if (!snapshot) {
            continue;
        }
        if (snapshot.stride == 0) {
            continue;
        }

        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const void* sample = snapshot.at(i);
            if (!sample) {
                continue;
            }
            const double ts = series->get_timestamp(sample);
            if (!std::isfinite(ts)) {
                continue;
            }
            if (ts < window_tmin || ts > window_tmax) {
                continue;
            }

            float low = 0.0f;
            float high = 0.0f;
            if (series->access.get_range) {
                std::tie(low, high) = series->get_range(sample);
            }
            else
            if (series->access.get_value) {
                low = series->get_value(sample);
                high = low;
            }
            else {
                continue;
            }

            const double dlow = std::min<double>(low, high);
            const double dhigh = std::max<double>(low, high);
            if (!std::isfinite(dlow) || !std::isfinite(dhigh)) {
                continue;
            }

            include_sample(ts, dlow, dhigh);
        }
    }

    if (!have_any) {
        return;
    }

    if (anchor_zero) {
        agg.vmin = std::min(agg.vmin, 0.0);
        agg.vmax = std::max(agg.vmax, 0.0);
    }

    const double scale = std::max(0.0, 1.0 + extra_v_scale);
    const double base_span = std::max(0.0, agg.vmax - agg.vmin);
    double span = base_span * scale;
    const double min_span = static_cast<double>(
        min_v_span_for(static_cast<float>(agg.vmin), static_cast<float>(agg.vmax)));
    if (!(span > min_span)) {
        span = std::max(span, min_span);
    }

    double new_vmin = 0.0;
    double new_vmax = 0.0;
    if (anchor_zero) {
        // Anchor to zero: keep vmin at the computed minimum, extend upward
        new_vmin = agg.vmin;
        new_vmax = new_vmin + span;
    }
    else {
        // Center the range around the data
        const double v_center = 0.5 * (agg.vmin + agg.vmax);
        new_vmin = v_center - span * 0.5;
        new_vmax = v_center + span * 0.5;
    }

    if (adjust_t && !(agg.tmax > agg.tmin)) {
        adjust_t = false;
    }

    const bool has_time_axis = (m_time_axis != nullptr);
    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.v_manual_min = static_cast<float>(new_vmin);
        m_data_cfg.v_manual_max = static_cast<float>(new_vmax);
        m_data_cfg.v_min = static_cast<float>(new_vmin);
        m_data_cfg.v_max = static_cast<float>(new_vmax);
    }

    if (adjust_t && has_time_axis) {
        m_time_axis->set_t_range(agg.tmin, agg.tmax);
    }
    else if (adjust_t) {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.t_min = agg.tmin;
        m_data_cfg.t_max = agg.tmax;
    }

    set_v_auto(false);

    emit v_limits_changed();
    if (adjust_t && !has_time_axis) {
        emit t_limits_changed();
    }
    update();
}

bool Plot_widget::can_zoom_in() const
{
    const auto cfg = data_cfg_snapshot();
    return (cfg.t_max - cfg.t_min) > 0.1;
}

QVariantList Plot_widget::get_indicator_samples(double x, double plot_width, double plot_height) const
{
    QVariantList result;

    if (plot_width <= 0.0 || plot_height <= 0.0) {
        return result;
    }

    const auto cfg = data_cfg_snapshot();
    const double tmin = cfg.t_min;
    const double tmax = cfg.t_max;
    float vmin = 0.0f;
    float vmax = 0.0f;
    if (m_v_auto.load(std::memory_order_acquire)) {
        vmin = cfg.v_min;
        vmax = cfg.v_max;
    }
    else {
        vmin = cfg.v_manual_min;
        vmax = cfg.v_manual_max;
    }

    const double t_span = tmax - tmin;
    const float v_span = vmax - vmin;

    if (t_span <= 0.0 || v_span <= 0.0f) {
        return result;
    }

    auto series_map = get_series_snapshot();

    for (const auto& [id, series] : series_map) {
        if (!series || !series->enabled) {
            continue;
        }
        if (!series->data_source || !series->access.get_timestamp || !series->access.get_value) {
            continue;
        }

        auto snap = series->data_source->snapshot(0);
        if (!snap || snap.count == 0 || snap.stride == 0) {
            continue;
        }

        const std::size_t count = snap.count;
        const auto* base = static_cast<const std::uint8_t*>(snap.data);

        const double first_ts = series->get_timestamp(base);
        const double last_ts = series->get_timestamp(base + (count - 1) * snap.stride);
        const bool ascending = first_ts <= last_ts;

        std::size_t lo = 0;
        std::size_t hi = count - 1;
        while (lo < hi) {
            std::size_t mid = (lo + hi) / 2;
            const double ts = series->get_timestamp(base + mid * snap.stride);
            if (ascending ? (ts < x) : (ts > x)) {
                lo = mid + 1;
            }
            else {
                hi = mid;
            }
        }

        std::size_t i0 = 0;
        std::size_t i1 = 0;

        if (count > 1) {
            if (ascending) {
                if (x <= first_ts) {
                    i0 = 0;
                    i1 = 0;
                }
                else
                if (x >= last_ts) {
                    i0 = count - 1;
                    i1 = count - 1;
                }
                else {
                    i0 = lo > 0 ? lo - 1 : 0;
                    i1 = lo;
                }
            }
            else {
                if (x >= first_ts) {
                    i0 = 0;
                    i1 = 0;
                }
                else
                if (x <= last_ts) {
                    i0 = count - 1;
                    i1 = count - 1;
                }
                else {
                    i0 = lo > 0 ? lo - 1 : 0;
                    i1 = lo;
                }
            }
        }

        const double x0 = series->get_timestamp(base + i0 * snap.stride);
        const double x1 = series->get_timestamp(base + i1 * snap.stride);
        const double y0 = static_cast<double>(series->get_value(base + i0 * snap.stride));
        const double y1 = static_cast<double>(series->get_value(base + i1 * snap.stride));

        double y = y0;
        const double denom = x1 - x0;
        if (i0 != i1 && std::abs(denom) > 1e-15) {
            double t = (x - x0) / denom;
            t = std::clamp(t, 0.0, 1.0);
            y = y0 + t * (y1 - y0);
        }

        double px = (x - tmin) / t_span * plot_width;
        double py = (1.0 - (y - vmin) / v_span) * plot_height;

        px = std::clamp(px, 0.0, plot_width);
        py = std::clamp(py, 0.0, plot_height);

        QColor color(
            static_cast<int>(series->color.r * 255.0f),
            static_cast<int>(series->color.g * 255.0f),
            static_cast<int>(series->color.b * 255.0f),
            static_cast<int>(series->color.a * 255.0f)
        );

        QVariantMap entry;
        entry["x"] = x;
        entry["y"] = y;
        entry["px"] = px;
        entry["py"] = py;
        entry["color"] = color;
        result.append(entry);
    }

    return result;
}

std::pair<float, float> Plot_widget::manual_v_range() const
{
    const auto cfg = data_cfg_snapshot();
    return {cfg.v_manual_min, cfg.v_manual_max};
}

std::pair<float, float> Plot_widget::current_v_range() const
{
    const auto cfg = data_cfg_snapshot();
    if (m_v_auto.load(std::memory_order_acquire)) {
        return {cfg.v_min, cfg.v_max};
    }
    return {cfg.v_manual_min, cfg.v_manual_max};
}

data_config_t Plot_widget::data_cfg_snapshot() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg;
}

void Plot_widget::sync_time_axis_state()
{
    if (!m_time_axis) {
        return;
    }

    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.t_min = m_time_axis->t_min();
        m_data_cfg.t_max = m_time_axis->t_max();
        m_data_cfg.t_available_min = m_time_axis->t_available_min();
        m_data_cfg.t_available_max = m_time_axis->t_available_max();
    }

    emit t_limits_changed();
    update();
}

void Plot_widget::clear_time_axis()
{
    m_time_axis = nullptr;
    m_time_axis_connection = {};
    m_time_axis_destroyed_connection = {};
    m_time_axis_vbar_connection = {};
    emit time_axis_changed();
    update();
}

bool Plot_widget::rendered_v_range(float& out_min, float& out_max) const
{
    if (!m_rendered_v_range_valid.load(std::memory_order_acquire)) {
        return false;
    }
    out_min = m_rendered_v_min.load(std::memory_order_acquire);
    out_max = m_rendered_v_max.load(std::memory_order_acquire);
    return true;
}

bool Plot_widget::consume_view_state_reset_request()
{
    return m_view_state_reset_requested.exchange(false, std::memory_order_acq_rel);
}

void Plot_widget::set_rendered_v_range(float v_min, float v_max) const
{
    if (!std::isfinite(v_min) || !std::isfinite(v_max) || v_min > v_max) {
        m_rendered_v_range_valid.store(false, std::memory_order_release);
        return;
    }
    m_rendered_v_min.store(v_min, std::memory_order_release);
    m_rendered_v_max.store(v_max, std::memory_order_release);
    m_rendered_v_range_valid.store(true, std::memory_order_release);
}

void Plot_widget::adjust_t_to_target(double target_tmin, double target_tmax)
{
    if (m_time_axis) {
        m_time_axis->adjust_t_to_target(target_tmin, target_tmax);
        return;
    }
    if (!(target_tmax > target_tmin)) {
        return;
    }

    const auto cfg = data_cfg_snapshot();
    const double avail_min = cfg.t_available_min;
    const double avail_max = cfg.t_available_max;

    const double avail_span = avail_max - avail_min;
    double span = target_tmax - target_tmin;
    if (avail_span > 0.0 && span > avail_span) {
        span = avail_span;
    }

    const double center = 0.5 * (target_tmin + target_tmax);
    double new_min = center - span * 0.5;
    double new_max = center + span * 0.5;

    if (avail_span > 0.0) {
        if (new_max > avail_max) {
            new_max = avail_max;
            new_min = new_max - span;
        }
        if (new_min < avail_min) {
            new_min = avail_min;
            new_max = new_min + span;
        }
    }

    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.t_min = new_min;
        m_data_cfg.t_max = new_max;
    }

    emit t_limits_changed();
    update();
}

double Plot_widget::compute_preview_height_px(double widget_height_px) const
{
    if (m_relative_preview_height <= 0.0f) {
        return 0.0;
    }

    const double font_h = m_base_label_height;
    const double available = std::max(0.0, widget_height_px - font_h);
    if (available <= 0.0) {
        return 0.0;
    }

    const double r = std::clamp<double>(m_relative_preview_height, 0.0, 1.0);
    if (r <= 0.0) {
        return 0.0;
    }

    double preview_px = (r * available) / (1.0 + r);

    const double min_px = m_preview_height_min > 0.0 ? m_preview_height_min * m_scaling_factor : 0.0;
    const double max_px = m_preview_height_max > 0.0
        ? m_preview_height_max * m_scaling_factor
        : std::numeric_limits<double>::infinity();

    if (preview_px > max_px) {
        preview_px = max_px;
    }

    if (preview_px < min_px) {
        if (!m_show_if_calculated_preview_height_below_min) {
            return 0.0;
        }
        preview_px = min_px;
    }

    if (m_preview_height_steps > 0 && max_px > min_px) {
        const int steps = m_preview_height_steps;
        const double delta = (max_px - min_px) / static_cast<double>(steps);
        if (delta > 0.0) {
            double clamped = std::clamp(preview_px, min_px, max_px);
            const double t = (clamped - min_px) / delta;
            int idx = static_cast<int>(std::floor(t));
            if (idx < 0) {
                idx = 0;
            }
            else
            if (idx > steps) {
                idx = steps;
            }
            preview_px = min_px + delta * static_cast<double>(idx);
        }
    }

    preview_px = std::clamp(preview_px, 0.0, available);
    return preview_px;
}

void Plot_widget::recalculate_preview_height()
{
    const double widget_h_dp = height();
    const double widget_h_px = widget_h_dp * m_scaling_factor;

    const double new_adjusted = compute_preview_height_px(widget_h_px);
    const double new_dp = (m_scaling_factor > 0.0)
        ? new_adjusted / m_scaling_factor
        : 0.0;

    const bool changed_target =
        (std::abs(new_dp - m_preview_height_target) > 0.5) ||
        (!m_preview_height_initialized && std::abs(new_dp - m_preview_height) > 0.5);

    if (!changed_target) {
        return;
    }

    m_preview_height_target = new_dp;

    if (!m_preview_height_initialized) {
        m_preview_height_initialized = true;
        m_preview_height = new_dp;
        m_adjusted_preview_height = new_adjusted;
        emit preview_height_changed();
    }

    emit preview_height_target_changed(m_preview_height_target);
}

QQuickFramebufferObject::Renderer* Plot_widget::createRenderer() const
{
    return new Plot_renderer(this);
}

void Plot_widget::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickFramebufferObject::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size()) {
        m_visible.store(newGeometry.width() > 0 && newGeometry.height() > 0, std::memory_order_release);
        recalculate_preview_height();
        update();
    }
}

void Plot_widget::timerEvent(QTimerEvent* ev)
{
    if (ev->timerId() == m_vbar_width_timer.timerId()) {
        constexpr double k_vbar_anim_duration_ms = 200.0;

        const double elapsed_ms = static_cast<double>(m_vbar_width_anim_elapsed.elapsed());
        double t = (k_vbar_anim_duration_ms > 0.0)
            ? (elapsed_ms / k_vbar_anim_duration_ms)
            : 1.0;
        if (t >= 1.0) {
            t = 1.0;
        }

        const double new_px =
            m_vbar_width_anim_start_px +
            (m_vbar_width_anim_target_px - m_vbar_width_anim_start_px) * t;

        m_vbar_width_px.store(new_px, std::memory_order_release);
        emit vbar_width_changed();
        update();

        if (t >= 1.0) {
            m_vbar_width_timer.stop();
        }
        return;
    }

    QQuickFramebufferObject::timerEvent(ev);
}

} // namespace vnm::plot
