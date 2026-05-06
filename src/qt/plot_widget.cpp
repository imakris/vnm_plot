#include <vnm_plot/qt/plot_widget.h>
#include "plot_renderer.h"
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
#include <type_traits>
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

template <typename Signature>
bool function_targets_equivalent(
    const std::function<Signature>& lhs,
    const std::function<Signature>& rhs)
{
    if (static_cast<bool>(lhs) != static_cast<bool>(rhs)) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    if (lhs.target_type() != rhs.target_type()) {
        return false;
    }

    using function_ptr_t = std::add_pointer_t<Signature>;
    const auto* lhs_fn = lhs.template target<function_ptr_t>();
    const auto* rhs_fn = rhs.template target<function_ptr_t>();
    if (lhs_fn || rhs_fn) {
        return lhs_fn && rhs_fn && (*lhs_fn == *rhs_fn);
    }

    // For stateful callables (lambdas/functors), there is no portable way to
    // compare captured state inside std::function. Treat same target type as
    // equivalent and rely on explicit revision fields where needed.
    return true;
}

bool plot_config_equivalent(
    const vnm::plot::Plot_config& lhs,
    const vnm::plot::Plot_config& rhs)
{
    // If a field is added to Plot_config, update this comparator and bump field_count.
    static_assert(vnm::plot::Plot_config::field_count == 22,
        "Plot_config field_count changed — update plot_config_equivalent to cover new fields");
    return
        lhs.dark_mode == rhs.dark_mode &&
        lhs.show_text == rhs.show_text &&
        lhs.grid_visibility == rhs.grid_visibility &&
        lhs.preview_visibility == rhs.preview_visibility &&
        function_targets_equivalent(lhs.format_timestamp, rhs.format_timestamp) &&
        lhs.format_timestamp_revision == rhs.format_timestamp_revision &&
        lhs.profiler.get() == rhs.profiler.get() &&
        lhs.font_size_px == rhs.font_size_px &&
        lhs.base_label_height_px == rhs.base_label_height_px &&
        function_targets_equivalent(lhs.log_debug, rhs.log_debug) &&
        function_targets_equivalent(lhs.log_error, rhs.log_error) &&
        function_targets_equivalent(lhs.register_assets, rhs.register_assets) &&
        lhs.assets_revision == rhs.assets_revision &&
        lhs.preview_height_px == rhs.preview_height_px &&
        lhs.clear_to_transparent == rhs.clear_to_transparent &&
        lhs.snap_lines_to_pixels == rhs.snap_lines_to_pixels &&
        lhs.line_width_px == rhs.line_width_px &&
        lhs.area_fill_alpha == rhs.area_fill_alpha &&
        lhs.allow_renderer_self_scheduling == rhs.allow_renderer_self_scheduling &&
        lhs.auto_v_range_mode == rhs.auto_v_range_mode &&
        lhs.auto_v_range_extra_scale == rhs.auto_v_range_extra_scale;
}

} // anonymous namespace

namespace vnm::plot {
using detail::k_vbar_width_change_threshold_d;
using detail::min_v_span_for;

Plot_widget::Plot_widget()
    : QQuickRhiItem()
{
    vnm_plot_init_qt_resources();

    m_relative_preview_height = 0.3f;
    m_preview_height_min = 30.0;
    m_preview_height_max = 150.0;
    m_show_if_calculated_preview_height_below_min = false;
    m_preview_height_steps = 2;

    update_dpi_scaling_factor();

    // Keep the compositor texture coordinates unmirrored. Backend clip-space
    // and framebuffer orientation are handled by the renderer's QRhi
    // projection correction.
    setMirrorVertically(false);
    setAlphaBlending(true);

    // Match the multisample count used by the benchmark and examples.
    setSampleCount(k_msaa_samples);
    setFlag(ItemHasContents, true);
}

Plot_widget::~Plot_widget()
{
    m_vbar_width_timer.stop();

    if (m_time_axis) {
        if (m_time_axis->sync_vbar_width()) {
            m_time_axis->clear_shared_vbar_width(this);
        }
        if (m_time_axis->indicator_owned_by(this)) {
            m_time_axis->set_indicator_state(this, false, 0.0);
        }
        QObject::disconnect(m_time_axis, nullptr, this, nullptr);
        m_time_axis_connection = {};
        m_time_axis_destroyed_connection = {};
        m_time_axis_vbar_connection = {};
        m_time_axis = nullptr;
    }
}

void Plot_widget::add_series(int id, std::shared_ptr<series_data_t> series)
{
    apply_series_updates({{id, std::move(series)}});
}

void Plot_widget::apply_series_updates(const std::vector<std::pair<int, std::shared_ptr<series_data_t>>>& updates)
{
    if (updates.empty()) {
        return;
    }

    std::vector<std::pair<int, std::shared_ptr<const series_data_t>>> copies;
    copies.reserve(updates.size());
    for (const auto& [id, series] : updates) {
        if (series) {
            copies.emplace_back(id, std::make_shared<series_data_t>(*series));
        }
        else {
            copies.emplace_back(id, std::shared_ptr<const series_data_t>{});
        }
    }

    {
        std::unique_lock lock(m_series_mutex);
        for (auto& [id, series] : copies) {
            m_series[id] = std::move(series);
        }
    }

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

std::map<int, std::shared_ptr<const series_data_t>> Plot_widget::get_series_snapshot() const
{
    std::shared_lock lock(m_series_mutex);
    return m_series;
}

void Plot_widget::set_config(const Plot_config& config)
{
    Plot_config effective_config;

    {
        std::unique_lock lock(m_config_mutex);
        const Plot_config prev_config = m_config;
        const double prev_grid_visibility = m_config.grid_visibility;
        const double prev_preview_visibility = m_config.preview_visibility;
        const double prev_line_width_px = m_config.line_width_px;
        m_config = config;
        m_config.grid_visibility = prev_grid_visibility;      // Preserve QML-controlled setting
        m_config.preview_visibility = prev_preview_visibility; // Preserve QML-controlled setting
        m_config.line_width_px = prev_line_width_px;          // Preserve QML-controlled setting
        if (!plot_config_equivalent(prev_config, m_config)) {
            m_config_revision.fetch_add(1, std::memory_order_relaxed);
        }
        effective_config = m_config;
    }
    m_adjusted_font_size = effective_config.font_size_px * m_scaling_factor;
    m_base_label_height = effective_config.base_label_height_px * m_scaling_factor;
    if (effective_config.preview_height_px > 0.0) {
        set_preview_height(effective_config.preview_height_px);
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
    m_rendered_t_range_valid.store(false, std::memory_order_release);
    update();
}

bool Plot_widget::dark_mode() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config.dark_mode;
}

template <typename Field, typename Value, typename Signal>
void Plot_widget::update_config_field(Field& field, Value new_value, Signal signal)
{
    {
        std::unique_lock lock(m_config_mutex);
        if (field == new_value) {
            return;
        }
        field = static_cast<Field>(new_value);
        m_config_revision.fetch_add(1, std::memory_order_relaxed);
    }
    (this->*signal)();
    update();
}

void Plot_widget::set_dark_mode(bool dark)
{
    update_config_field(m_config.dark_mode, dark, &Plot_widget::dark_mode_changed);
}

double Plot_widget::grid_visibility() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config.grid_visibility;
}

void Plot_widget::set_grid_visibility(double visibility)
{
    update_config_field(
        m_config.grid_visibility,
        std::clamp(visibility, 0.0, 1.0),
        &Plot_widget::grid_visibility_changed);
}

double Plot_widget::preview_visibility() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config.preview_visibility;
}

void Plot_widget::set_preview_visibility(double visibility)
{
    update_config_field(
        m_config.preview_visibility,
        std::clamp(visibility, 0.0, 1.0),
        &Plot_widget::preview_visibility_changed);
}

double Plot_widget::line_width_px() const
{
    std::shared_lock lock(m_config_mutex);
    return m_config.line_width_px;
}

void Plot_widget::set_line_width_px(double width)
{
    update_config_field(m_config.line_width_px, width, &Plot_widget::line_width_px_changed);
}

qint64 Plot_widget::t_min() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.t_min;
}

qint64 Plot_widget::t_max() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.t_max;
}

qint64 Plot_widget::t_available_min() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.t_available_min;
}

qint64 Plot_widget::t_available_max() const
{
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.t_available_max;
}

qint64 Plot_widget::t_min_qml_ms() const
{
    return ns_to_ms_for_qml(t_min());
}

qint64 Plot_widget::t_max_qml_ms() const
{
    return ns_to_ms_for_qml(t_max());
}

qint64 Plot_widget::t_available_min_qml_ms() const
{
    return ns_to_ms_for_qml(t_available_min());
}

qint64 Plot_widget::t_available_max_qml_ms() const
{
    return ns_to_ms_for_qml(t_available_max());
}

void Plot_widget::set_t_range(qint64 t_min_ns, qint64 t_max_ns)
{
    if (!(t_max_ns > t_min_ns)) {
        return;
    }
    if (m_time_axis) {
        m_time_axis->set_t_range(t_min_ns, t_max_ns);
        return;
    }
    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.t_min = t_min_ns;
        m_data_cfg.t_max = t_max_ns;
    }
    emit t_limits_changed();
    update();
}

void Plot_widget::clamp_t_range_to_available(qint64 t_avail_min_ns, qint64 t_avail_max_ns)
{
    const qint64 span = t_avail_max_ns - t_avail_min_ns;
    const qint64 cur_span = m_data_cfg.t_max - m_data_cfg.t_min;
    if (cur_span > span) {
        m_data_cfg.t_min = t_avail_min_ns;
        m_data_cfg.t_max = t_avail_max_ns;
    }
    else {
        if (m_data_cfg.t_min < t_avail_min_ns) {
            m_data_cfg.t_min = t_avail_min_ns;
            m_data_cfg.t_max = t_avail_min_ns + cur_span;
        }
        if (m_data_cfg.t_max > t_avail_max_ns) {
            m_data_cfg.t_max = t_avail_max_ns;
            m_data_cfg.t_min = t_avail_max_ns - cur_span;
        }
    }
    m_data_cfg.t_available_min = t_avail_min_ns;
    m_data_cfg.t_available_max = t_avail_max_ns;
}

void Plot_widget::set_available_t_range(qint64 t_min_ns, qint64 t_max_ns)
{
    if (!(t_max_ns > t_min_ns)) {
        return;
    }
    if (m_time_axis) {
        m_time_axis->set_available_t_range(t_min_ns, t_max_ns);
        return;
    }
    {
        std::unique_lock lock(m_data_cfg_mutex);
        clamp_t_range_to_available(t_min_ns, t_max_ns);
    }
    emit t_limits_changed();
    update();
}

void Plot_widget::set_view(const Plot_view& view)
{
    bool t_changed = false;
    bool v_changed = false;

    const auto t_range_valid = [](const std::pair<qint64, qint64>& r) {
        return r.second > r.first;
    };
    const auto v_range_valid = [](const std::pair<float, float>& r) {
        return std::isfinite(r.first) && std::isfinite(r.second) && r.second > r.first;
    };

    const bool t_range_ok = view.t_range && t_range_valid(*view.t_range);
    const bool t_avail_ok = view.t_available_range && t_range_valid(*view.t_available_range);

    if (m_time_axis) {
        if (t_range_ok) {
            m_time_axis->set_t_range(view.t_range->first, view.t_range->second);
            t_changed = true;
        }
        if (t_avail_ok) {
            m_time_axis->set_available_t_range(view.t_available_range->first, view.t_available_range->second);
            t_changed = true;
        }
    }
    else
    if (t_range_ok || t_avail_ok) {
        std::unique_lock lock(m_data_cfg_mutex);
        if (t_range_ok) {
            m_data_cfg.t_min = view.t_range->first;
            m_data_cfg.t_max = view.t_range->second;
            t_changed = true;
        }
        if (t_avail_ok) {
            clamp_t_range_to_available(
                view.t_available_range->first,
                view.t_available_range->second);
            t_changed = true;
        }
    }

    if (view.v_range && v_range_valid(*view.v_range)) {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.v_min = view.v_range->first;
        m_data_cfg.v_max = view.v_range->second;
        m_data_cfg.v_manual_min = view.v_range->first;
        m_data_cfg.v_manual_max = view.v_range->second;
        v_changed = true;
    }

    bool v_auto_changed_flag = false;
    if (view.v_auto) {
        if (m_v_auto.exchange(*view.v_auto, std::memory_order_acq_rel) != *view.v_auto) {
            v_auto_changed_flag = true;
        }
    }

    if (t_changed) {
        emit t_limits_changed();
    }
    if (v_changed) {
        emit v_limits_changed();
    }
    if (v_auto_changed_flag) {
        emit v_auto_changed();
    }
    if (t_changed || v_changed || v_auto_changed_flag) {
        update();
    }
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
        // Seed an uninitialized axis from the widget's existing configuration
        // before pulling. This preserves a user-set view (typically applied
        // via set_view before the QML time_axis binding fires) and gives
        // subsequent mouse interactions a real span to operate on, since
        // adjust_t_from_* on Plot_time_axis is gated on view_initialized().
        // Subsequent attachments to a shared axis still pull through
        // sync_time_axis_state() because the axis is no longer uninitialized.
        {
            const auto cfg = data_cfg_snapshot();
            if (!m_time_axis->view_initialized() && cfg.t_max > cfg.t_min) {
                m_time_axis->set_t_range(cfg.t_min, cfg.t_max);
            }
            if (!m_time_axis->available_initialized()
                && cfg.t_available_max > cfg.t_available_min)
            {
                m_time_axis->set_available_t_range(
                    cfg.t_available_min, cfg.t_available_max);
            }
        }
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
    if (m_v_auto.load(std::memory_order_acquire)) {
        float rendered_min = 0.0f;
        float rendered_max = 0.0f;
        if (rendered_v_range(rendered_min, rendered_max)) {
            return rendered_min;
        }
        std::shared_lock lock(m_data_cfg_mutex);
        return m_data_cfg.v_min;
    }
    std::shared_lock lock(m_data_cfg_mutex);
    return m_data_cfg.v_manual_min;
}

float Plot_widget::v_max() const
{
    if (m_v_auto.load(std::memory_order_acquire)) {
        float rendered_min = 0.0f;
        float rendered_max = 0.0f;
        if (rendered_v_range(rendered_min, rendered_max)) {
            return rendered_max;
        }
        std::shared_lock lock(m_data_cfg_mutex);
        return m_data_cfg.v_max;
    }
    std::shared_lock lock(m_data_cfg_mutex);
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
    if (!std::isfinite(v_min) || !std::isfinite(v_max) || !(v_max > v_min)) {
        return;
    }
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
    if (!std::isfinite(target) || target <= 0.0) {
        return;
    }

    const double width_px = width() * m_scaling_factor;
    if (std::isfinite(width_px) && width_px > 0.0) {
        const double max_target = std::max(1.0, width_px * 0.5);
        target = std::clamp(target, 1.0, max_target);
    }

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
    // Pixel deltas are double; the visible span is in int64 nanoseconds.
    // Subtract first, scale through fp64 once, round into qint64.
    const qint64 span_ns = cfg.t_max - cfg.t_min;
    const qint64 delta_ns = static_cast<qint64>(std::llround(
        diff * static_cast<double>(span_ns) / ref_width));
    adjust_t_to_target(cfg.t_min - delta_ns, cfg.t_max - delta_ns);
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
    const qint64 avail_span_ns = cfg.t_available_max - cfg.t_available_min;
    const qint64 delta_ns = static_cast<qint64>(std::llround(
        diff * static_cast<double>(avail_span_ns) / ref_width));
    adjust_t_to_target(cfg.t_min + delta_ns, cfg.t_max + delta_ns);
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
    const qint64 span_ns = cfg.t_max - cfg.t_min;
    const qint64 avail_span_ns = cfg.t_available_max - cfg.t_available_min;
    const qint64 center_ns = cfg.t_available_min + static_cast<qint64>(std::llround(
        (x_pos / ref_width) * static_cast<double>(avail_span_ns)));
    const qint64 half_ns = span_ns / 2;
    adjust_t_to_target(center_ns - half_ns, center_ns + (span_ns - half_ns));
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
    const qint64 span_ns = cfg.t_max - cfg.t_min;
    const qint64 t_pivot_ns = cfg.t_min + static_cast<qint64>(std::llround(
        pivot * static_cast<double>(span_ns)));
    const qint64 left_ns = static_cast<qint64>(std::llround(
        static_cast<double>(t_pivot_ns - cfg.t_min) * scale));
    const qint64 right_ns = static_cast<qint64>(std::llround(
        static_cast<double>(cfg.t_max - t_pivot_ns) * scale));
    adjust_t_to_target(t_pivot_ns - left_ns, t_pivot_ns + right_ns);
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
    const qint64 window_tmin_ns = cfg.t_min;
    const qint64 window_tmax_ns = cfg.t_max;

    if (!(window_tmax_ns > window_tmin_ns)) {
        return;
    }

    struct aggregated_range_t
    {
        double       vmin;
        double       vmax;
        std::int64_t tmin_ns;
        std::int64_t tmax_ns;
    };

    bool have_any = false;
    aggregated_range_t agg{};

    const auto include_sample = [&](std::int64_t ts_ns, double low, double high) {
        if (!have_any) {
            agg = {low, high, ts_ns, ts_ns};
            have_any = true;
            return;
        }
        agg.vmin = std::min(agg.vmin, low);
        agg.vmax = std::max(agg.vmax, high);
        agg.tmin_ns = std::min(agg.tmin_ns, ts_ns);
        agg.tmax_ns = std::max(agg.tmax_ns, ts_ns);
    };

    std::vector<std::shared_ptr<const series_data_t>> sources;
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
        if (!series || !series->main_source() || !series->access.get_timestamp) {
            continue;
        }

        auto snapshot = series->main_source()->snapshot(0);
        if (!snapshot.is_valid()) {
            continue;
        }

        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const void* sample = snapshot.at(i);
            if (!sample) {
                continue;
            }
            const std::int64_t ts_ns = series->get_timestamp(sample);
            if (ts_ns < window_tmin_ns || ts_ns > window_tmax_ns) {
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

            include_sample(ts_ns, dlow, dhigh);
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

    if (adjust_t && !(agg.tmax_ns > agg.tmin_ns)) {
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
        m_time_axis->set_t_range(agg.tmin_ns, agg.tmax_ns);
    }
    else if (adjust_t) {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.t_min = agg.tmin_ns;
        m_data_cfg.t_max = agg.tmax_ns;
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
    // 0.1 second is the floor for zoom-in (== 100 ms). Below that the axis
    // is dominated by tick-label clutter and gives no actionable detail.
    constexpr std::int64_t k_min_zoom_span_ns = std::int64_t{100} * 1'000'000;
    return (cfg.t_max - cfg.t_min) > k_min_zoom_span_ns;
}

QVariantList Plot_widget::get_indicator_samples(
    double x_ms,
    double plot_width,
    double plot_height,
    double mouse_px) const
{
    QVariantList result;

    if (plot_width <= 0.0 || plot_height <= 0.0) {
        return result;
    }

    const auto cfg = data_cfg_snapshot();
    qint64 tmin_ns = 0;
    qint64 tmax_ns = 0;
    if (!rendered_t_range(tmin_ns, tmax_ns)) {
        tmin_ns = cfg.t_min;
        tmax_ns = cfg.t_max;
    }
    // QML passes x_ms in milliseconds-since-epoch; the rest of this routine
    // works in nanoseconds (cast to fp64 for arithmetic, as before). Bring
    // x onto the same axis up front. entry["x"] is converted back to ms at
    // the end so the QML side stays on the ms surface end-to-end.
    constexpr double k_ns_per_ms = 1'000'000.0;
    double x = x_ms * k_ns_per_ms;
    const double tmin = static_cast<double>(tmin_ns);
    const double tmax = static_cast<double>(tmax_ns);
    float vmin = 0.0f;
    float vmax = 0.0f;
    if (m_v_auto.load(std::memory_order_acquire)) {
        if (!rendered_v_range(vmin, vmax)) {
            vmin = cfg.v_min;
            vmax = cfg.v_max;
        }
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

    // Recompute target time from rendered range when the caller supplies pixel x.
    // This keeps pixel->time and time->pixel conversions on the same range and
    // avoids horizontal indicator jitter during high-rate loading.
    if (mouse_px >= 0.0) {
        x = tmin + (mouse_px / plot_width) * t_span;
    }

    auto series_map = get_series_snapshot();

    for (const auto& [id, series] : series_map) {
        if (!series || !series->enabled) {
            continue;
        }
        if (!series->main_source() || !series->access.get_timestamp || !series->access.get_value) {
            continue;
        }

        auto snap = series->main_source()->snapshot(0);
        if (!snap.is_valid()) {
            continue;
        }

        const std::size_t count = snap.count;
        const auto sample_at = [&](std::size_t index) -> const void* {
            return snap.at(index);
        };

        const void* first_sample = sample_at(0);
        const void* last_sample = sample_at(count - 1);
        if (!first_sample || !last_sample) {
            continue;
        }

        const double first_ts = series->get_timestamp(first_sample);
        const double last_ts = series->get_timestamp(last_sample);
        const bool ascending = first_ts <= last_ts;

        std::size_t lo = 0;
        std::size_t hi = count - 1;
        while (lo < hi) {
            std::size_t mid = (lo + hi) / 2;
            const void* mid_sample = sample_at(mid);
            if (!mid_sample) {
                lo = hi;
                break;
            }
            const double ts = series->get_timestamp(mid_sample);
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

        const void* sample0 = sample_at(i0);
        const void* sample1 = sample_at(i1);
        if (!sample0 || !sample1) {
            continue;
        }

        const double x0 = series->get_timestamp(sample0);
        const double x1 = series->get_timestamp(sample1);
        const double y0 = static_cast<double>(series->get_value(sample0));
        const double y1 = static_cast<double>(series->get_value(sample1));

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
        // x_ms keeps the QML side on the milliseconds-since-epoch surface;
        // see the Plot_widget class-level comment for the convention.
        entry["x"] = x / k_ns_per_ms;
        entry["y"] = y;
        entry["px"] = px;
        entry["py"] = py;
        entry["color"] = color;
        entry["series_label"] = QString::fromStdString(series->series_label);
        result.append(entry);
    }

    return result;
}

QString Plot_widget::format_timestamp_precise(qint64 timestamp_ms) const
{
    const auto cfg = config();
    const auto formatter = cfg.format_timestamp ? cfg.format_timestamp : default_format_timestamp;
    // QML passes timestamp_ms (milliseconds-since-epoch); the formatter
    // expects nanoseconds. Convert at the boundary.
    const qint64 timestamp_ns = ms_for_qml_to_ns(timestamp_ms);
    // Step is zero ns: this caller asks for a single-instant rendering, not
    // a tick label, so the step argument is moot.
    return QString::fromStdString(formatter(timestamp_ns, std::int64_t{0}));
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
        float rendered_min = 0.0f;
        float rendered_max = 0.0f;
        if (rendered_v_range(rendered_min, rendered_max)) {
            return {rendered_min, rendered_max};
        }
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

    // Only pull bounds the axis has actually been told about. A freshly-
    // attached axis reports k_t_unset for every slot; pulling those into
    // m_data_cfg would silently overwrite a view that was set via set_view
    // before the axis was attached.
    const bool view_init = m_time_axis->view_initialized();
    const bool available_init = m_time_axis->available_initialized();
    if (!view_init && !available_init) {
        return;
    }

    {
        std::unique_lock lock(m_data_cfg_mutex);
        if (view_init) {
            m_data_cfg.t_min = m_time_axis->t_min();
            m_data_cfg.t_max = m_time_axis->t_max();
        }
        if (available_init) {
            m_data_cfg.t_available_min = m_time_axis->t_available_min();
            m_data_cfg.t_available_max = m_time_axis->t_available_max();
        }
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

bool Plot_widget::rendered_t_range(qint64& out_min_ns, qint64& out_max_ns) const
{
    if (!m_rendered_t_range_valid.load(std::memory_order_acquire)) {
        return false;
    }
    out_min_ns = m_rendered_t_min.load(std::memory_order_acquire);
    out_max_ns = m_rendered_t_max.load(std::memory_order_acquire);
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
    m_rendered_v_range_valid.store(false, std::memory_order_release);
    m_rendered_v_min.store(v_min, std::memory_order_relaxed);
    m_rendered_v_max.store(v_max, std::memory_order_relaxed);
    m_rendered_v_range_valid.store(true, std::memory_order_release);
}

void Plot_widget::set_rendered_t_range(qint64 t_min_ns, qint64 t_max_ns) const
{
    // Time span must be strictly positive to keep time<->pixel conversions safe.
    if (t_max_ns <= t_min_ns) {
        m_rendered_t_range_valid.store(false, std::memory_order_release);
        return;
    }
    m_rendered_t_range_valid.store(false, std::memory_order_release);
    m_rendered_t_min.store(t_min_ns, std::memory_order_relaxed);
    m_rendered_t_max.store(t_max_ns, std::memory_order_relaxed);
    m_rendered_t_range_valid.store(true, std::memory_order_release);
}

void Plot_widget::adjust_t_to_target(qint64 target_tmin_ns, qint64 target_tmax_ns)
{
    if (m_time_axis) {
        m_time_axis->adjust_t_to_target(target_tmin_ns, target_tmax_ns);
        return;
    }
    if (!(target_tmax_ns > target_tmin_ns)) {
        return;
    }

    const auto cfg = data_cfg_snapshot();
    const qint64 avail_min_ns = cfg.t_available_min;
    const qint64 avail_max_ns = cfg.t_available_max;

    const qint64 avail_span_ns = avail_max_ns - avail_min_ns;
    qint64 span_ns = target_tmax_ns - target_tmin_ns;
    if (avail_span_ns > 0 && span_ns > avail_span_ns) {
        span_ns = avail_span_ns;
    }

    // Use integer midpoint that rounds toward zero; the resulting +- half_ns
    // pair sums back to span_ns regardless of parity.
    const qint64 half_ns = span_ns / 2;
    const qint64 center_ns = target_tmin_ns + (target_tmax_ns - target_tmin_ns) / 2;
    qint64 new_min_ns = center_ns - half_ns;
    qint64 new_max_ns = new_min_ns + span_ns;

    if (avail_span_ns > 0) {
        if (new_max_ns > avail_max_ns) {
            new_max_ns = avail_max_ns;
            new_min_ns = new_max_ns - span_ns;
        }
        if (new_min_ns < avail_min_ns) {
            new_min_ns = avail_min_ns;
            new_max_ns = new_min_ns + span_ns;
        }
    }

    {
        std::unique_lock lock(m_data_cfg_mutex);
        m_data_cfg.t_min = new_min_ns;
        m_data_cfg.t_max = new_max_ns;
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

QQuickRhiItemRenderer* Plot_widget::createRenderer()
{
    return new Plot_renderer(this);
}

void Plot_widget::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickRhiItem::geometryChange(newGeometry, oldGeometry);

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

    QQuickRhiItem::timerEvent(ev);
}

} // namespace vnm::plot
