#include <vnm_plot/plot_widget.h>
#include <vnm_plot/plot_renderer.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/algo.h>

#include <QGuiApplication>
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
        m_config = config;
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
}

void Plot_widget::set_vbar_width_from_renderer(double px)
{
    const double current = m_vbar_width_px.load(std::memory_order_acquire);
    const double target = px;

    if (std::abs(target - current) <= core::constants::k_vbar_width_change_threshold_d &&
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

void Plot_widget::set_auto_v_range_from_renderer(float v_min, float v_max)
{
    if (!std::isfinite(v_min) || !std::isfinite(v_max)) {
        return;
    }

    if (!m_v_auto.load(std::memory_order_acquire)) {
        return;
    }

    constexpr float k_eps = 1e-6f;
    bool changed = false;
    {
        std::unique_lock lock(m_data_cfg_mutex);
        if (std::abs(m_data_cfg.v_min - v_min) > k_eps ||
            std::abs(m_data_cfg.v_max - v_max) > k_eps)
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
    if (ref_width <= 0.0) {
        return;
    }

    double t_min_val = 0.0;
    double t_max_val = 0.0;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        t_min_val = m_data_cfg.t_min;
        t_max_val = m_data_cfg.t_max;
    }

    const double span = t_max_val - t_min_val;
    const double delta = diff * span / ref_width;
    adjust_t_to_target(t_min_val - delta, t_max_val - delta);
}

void Plot_widget::adjust_t_from_mouse_diff_on_preview(double ref_width, double diff)
{
    if (ref_width <= 0.0) {
        return;
    }

    double t_min_val = 0.0;
    double t_max_val = 0.0;
    double avail_min = 0.0;
    double avail_max = 0.0;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        t_min_val = m_data_cfg.t_min;
        t_max_val = m_data_cfg.t_max;
        avail_min = m_data_cfg.t_available_min;
        avail_max = m_data_cfg.t_available_max;
    }

    const double avail_span = avail_max - avail_min;
    const double delta = diff * avail_span / ref_width;
    adjust_t_to_target(t_min_val + delta, t_max_val + delta);
}

void Plot_widget::adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos)
{
    if (ref_width <= 0.0) {
        return;
    }

    double t_min_val = 0.0;
    double t_max_val = 0.0;
    double avail_min = 0.0;
    double avail_max = 0.0;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        t_min_val = m_data_cfg.t_min;
        t_max_val = m_data_cfg.t_max;
        avail_min = m_data_cfg.t_available_min;
        avail_max = m_data_cfg.t_available_max;
    }

    const double span = t_max_val - t_min_val;
    const double avail_span = avail_max - avail_min;
    const double rel = x_pos / ref_width;
    const double new_center = avail_min + rel * avail_span;
    adjust_t_to_target(new_center - span * 0.5, new_center + span * 0.5);
}

void Plot_widget::adjust_t_from_pivot_and_scale(double pivot, double scale)
{
    if (scale <= 0.0) {
        return;
    }

    double t_min_val = 0.0;
    double t_max_val = 0.0;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        t_min_val = m_data_cfg.t_min;
        t_max_val = m_data_cfg.t_max;
    }

    const double t_pivot = t_min_val + (t_max_val - t_min_val) * pivot;
    const double new_min = t_pivot - (t_pivot - t_min_val) * scale;
    const double new_max = t_pivot + (t_max_val - t_pivot) * scale;
    adjust_t_to_target(new_min, new_max);
}

void Plot_widget::pan_time(double delta_px, double viewport_width)
{
    if (viewport_width <= 0.0) {
        return;
    }

    double t_min_val, t_max_val;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        t_min_val = m_data_cfg.t_min;
        t_max_val = m_data_cfg.t_max;
    }

    const double t_span = t_max_val - t_min_val;
    const double delta_t = (delta_px / viewport_width) * t_span;

    adjust_t_to_target(t_min_val - delta_t, t_max_val - delta_t);
}

void Plot_widget::zoom_time(double pivot, double scale)
{
    if (scale <= 0.0) {
        return;
    }

    double t_min_val, t_max_val;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        t_min_val = m_data_cfg.t_min;
        t_max_val = m_data_cfg.t_max;
    }

    const double new_t_min = pivot - (pivot - t_min_val) * scale;
    const double new_t_max = pivot + (t_max_val - pivot) * scale;

    adjust_t_to_target(new_t_min, new_t_max);
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

void Plot_widget::pan_value(float delta_px, float viewport_height)
{
    if (viewport_height <= 0.0f) {
        return;
    }

    const auto [v_min_val, v_max_val] = current_v_range();

    const float v_span = v_max_val - v_min_val;
    const float delta_v = (delta_px / viewport_height) * v_span;

    adjust_v_to_target(v_min_val + delta_v, v_max_val + delta_v);
}

void Plot_widget::zoom_value(float pivot, float scale)
{
    if (scale <= 0.0f) {
        return;
    }

    const auto [v_min_val, v_max_val] = current_v_range();

    const float new_v_min = pivot - (pivot - v_min_val) * scale;
    const float new_v_max = pivot + (v_max_val - pivot) * scale;

    adjust_v_to_target(new_v_min, new_v_max);
}

void Plot_widget::adjust_v_to_target(float target_vmin, float target_vmax)
{
    const float min_span = core::algo::min_v_span_for(target_vmin, target_vmax);
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
    double window_tmin = 0.0;
    double window_tmax = 0.0;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        window_tmin = m_data_cfg.t_min;
        window_tmax = m_data_cfg.t_max;
    }

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

        const auto* base = static_cast<const std::uint8_t*>(snapshot.data);
        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const void* sample = base + i * snapshot.stride;
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
        core::algo::min_v_span_for(static_cast<float>(agg.vmin), static_cast<float>(agg.vmax)));
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

    if (adjust_t) {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.v_manual_min = static_cast<float>(new_vmin);
        m_data_cfg.v_manual_max = static_cast<float>(new_vmax);
        m_data_cfg.v_min = static_cast<float>(new_vmin);
        m_data_cfg.v_max = static_cast<float>(new_vmax);
        m_data_cfg.t_min = agg.tmin;
        m_data_cfg.t_max = agg.tmax;
    }
    else {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.v_manual_min = static_cast<float>(new_vmin);
        m_data_cfg.v_manual_max = static_cast<float>(new_vmax);
        m_data_cfg.v_min = static_cast<float>(new_vmin);
        m_data_cfg.v_max = static_cast<float>(new_vmax);
    }

    set_v_auto(false);

    emit v_limits_changed();
    if (adjust_t) {
        emit t_limits_changed();
    }
    update();
}

bool Plot_widget::can_zoom_in() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return (m_data_cfg.t_max - m_data_cfg.t_min) > 0.1;
}

QVariantList Plot_widget::get_indicator_samples(double x, double plot_width, double plot_height) const
{
    QVariantList result;

    if (plot_width <= 0.0 || plot_height <= 0.0) {
        return result;
    }

    double tmin = 0.0;
    double tmax = 0.0;
    float vmin = 0.0f;
    float vmax = 0.0f;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        tmin = m_data_cfg.t_min;
        tmax = m_data_cfg.t_max;
        if (m_v_auto.load(std::memory_order_acquire)) {
            vmin = m_data_cfg.v_min;
            vmax = m_data_cfg.v_max;
        }
        else {
            vmin = m_data_cfg.v_manual_min;
            vmax = m_data_cfg.v_manual_max;
        }
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
                else if (x >= last_ts) {
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
                else if (x <= last_ts) {
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
    std::shared_lock lock(m_data_cfg_mutex);
    return {m_data_cfg.v_manual_min, m_data_cfg.v_manual_max};
}

std::pair<float, float> Plot_widget::current_v_range() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    if (m_v_auto.load(std::memory_order_acquire)) {
        return {m_data_cfg.v_min, m_data_cfg.v_max};
    }
    return {m_data_cfg.v_manual_min, m_data_cfg.v_manual_max};
}

void Plot_widget::adjust_t_to_target(double target_tmin, double target_tmax)
{
    if (!(target_tmax > target_tmin)) {
        return;
    }

    double avail_min = 0.0;
    double avail_max = 0.0;
    {
        std::shared_lock lock(m_data_cfg_mutex);
        avail_min = m_data_cfg.t_available_min;
        avail_max = m_data_cfg.t_available_max;
    }

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
