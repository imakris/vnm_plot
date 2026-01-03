#pragma once

// VNM Plot Library - Layout Calculator
// Computes axis labels, grid positions, and layout metrics.
// This is pure computational logic with no OpenGL dependencies.

#include "../plot_algo.h"
#include "../plot_config.h"
#include "../plot_types.h"

#include <glm/glm.hpp>

#include <QByteArray>
#include <QSize>
#include <QString>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Label Structures
// -----------------------------------------------------------------------------
struct v_label_t
{
    double     value;
    float      y;
    QByteArray text;
};

struct h_label_t
{
    double     value;
    glm::vec2  position;
    QByteArray text;
};

// -----------------------------------------------------------------------------
// Layout Result
// -----------------------------------------------------------------------------
struct frame_layout_result_t
{
    double usable_width            = 0.0;
    double usable_height           = 0.0;
    double v_bar_width             = 0.0;
    float  max_v_label_text_width  = 0.f;

    std::vector<h_label_t> h_labels;
    std::vector<v_label_t> v_labels;

    int    v_label_fixed_digits   = 0;
    bool   h_labels_subsecond     = false;

    int    vertical_seed_index    = -1;
    double vertical_seed_step     = 0.0;
    double vertical_finest_step   = 0.0;
    int    horizontal_seed_index  = -1;
    double horizontal_seed_step   = 0.0;
};

// -----------------------------------------------------------------------------
// Layout Calculator
// -----------------------------------------------------------------------------
// Computes axis labels and layout metrics based on data ranges and viewport.
// Stateless - all inputs provided via parameters struct.
class Layout_calculator
{
public:
    // All inputs for a layout calculation pass
    struct parameters_t
    {
        // Data ranges
        float  v_min;
        float  v_max;
        double t_min;
        double t_max;

        // Viewport dimensions
        double usable_width;
        double usable_height;
        double vbar_width;
        double label_visible_height;

        // Font metrics
        double    adjusted_font_size_in_pixels;
        float     h_label_vertical_nudge_factor = 0.0f;
        uint64_t  measure_text_cache_key        = 0;
        float     monospace_char_advance_px     = 0.f;
        bool      monospace_advance_is_reliable = false;

        // Callbacks for metrics and formatting
        std::function<int(double)>              get_required_fixed_digits_func;
        std::function<QString(double, double)>  format_timestamp_func;
        std::function<float(const char*)>       measure_text_func;

        // Optional profiler (from Plot_config)
        Profiler* profiler = nullptr;

        // Seed hints for incremental computation
        bool   has_vertical_seed     = false;
        int    vertical_seed_index   = -1;
        double vertical_seed_step    = 0.0;

        bool   has_horizontal_seed   = false;
        int    horizontal_seed_index = -1;
        double horizontal_seed_step  = 0.0;
    };

    // Calculation result
    struct result_t
    {
        std::vector<v_label_t> v_labels;
        std::vector<h_label_t> h_labels;

        int    v_label_fixed_digits   = 1;
        bool   h_labels_subsecond     = false;
        float  max_v_label_text_width = 0.f;

        int    vertical_seed_index    = -1;
        double vertical_seed_step     = 0.0;
        double vertical_finest_step   = 0.0;
        int    horizontal_seed_index  = -1;
        double horizontal_seed_step   = 0.0;
    };

    Layout_calculator() = default;

    // Main entry point - calculate layout from parameters
    result_t calculate(const parameters_t& params) const;

private:
    // Check if intervals fit without overlap
    bool fits_with_gap(
        const std::vector<std::pair<float, float>>& level,
        const std::vector<std::pair<float, float>>& accepted,
        float min_gap) const;

    // Scratch buffers (reused to avoid allocations)
    mutable std::vector<std::pair<double, float>> m_scratch_vals;
    mutable std::vector<std::pair<float, float>>  m_scratch_level;
    mutable std::vector<std::pair<float, float>>  m_scratch_accepted_boxes;
    mutable std::vector<float>                    m_scratch_accepted_y;
    mutable std::vector<double>                   m_scratch_vals_d;
};

// -----------------------------------------------------------------------------
// Layout Cache
// -----------------------------------------------------------------------------
// Caches layout results to avoid recomputation when inputs haven't changed.
class Layout_cache
{
public:
    struct key_t
    {
        float     v0                           = 0.0f;
        float     v1                           = 0.0f;
        double    t0                           = 0.0;
        double    t1                           = 0.0;
        QSize     viewport_size;
        double    adjusted_reserved_height     = 0.0;
        double    adjusted_preview_height      = 0.0;
        double    adjusted_font_size_in_pixels = 0.0;
        double    vbar_width_pixels            = 0.0;
        uint64_t  font_metrics_key             = 0;
    };

    using value_type = frame_layout_result_t;

    [[nodiscard]] const value_type* try_get(const key_t& query) const noexcept
    {
        if (!m_valid) {
            return nullptr;
        }
        if (m_key.v0 != query.v0 ||
            m_key.v1 != query.v1 ||
            m_key.t0 != query.t0 ||
            m_key.t1 != query.t1 ||
            m_key.viewport_size != query.viewport_size ||
            m_key.adjusted_reserved_height != query.adjusted_reserved_height ||
            m_key.adjusted_preview_height != query.adjusted_preview_height ||
            m_key.adjusted_font_size_in_pixels != query.adjusted_font_size_in_pixels ||
            m_key.vbar_width_pixels != query.vbar_width_pixels ||
            m_key.font_metrics_key != query.font_metrics_key)
        {
            return nullptr;
        }
        return &m_value;
    }

    [[nodiscard]] const value_type& store(const key_t& new_key, value_type&& new_value) noexcept
    {
        m_key   = new_key;
        m_value = std::move(new_value);
        m_valid = true;
        return m_value;
    }

    void invalidate() noexcept { m_valid = false; }

private:
    bool       m_valid = false;
    key_t      m_key{};
    value_type m_value{};
};

// Shutdown caches (call during application shutdown)
void shutdown_layout_caches();

} // namespace vnm::plot
