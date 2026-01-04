#pragma once

// VNM Plot Library - Core Layout Types
// Qt-free layout structures for axis labels and frame layout.

#include "core_types.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vnm::plot::core {

// -----------------------------------------------------------------------------
// Grid Layer Parameters (for multi-level grid rendering)
// -----------------------------------------------------------------------------

struct grid_layer_params_t
{
    static constexpr int k_max_levels = 32;

    int   count = 0;
    float spacing_px[k_max_levels]   = {};
    float start_px[k_max_levels]     = {};
    float alpha[k_max_levels]        = {};
    float thickness_px[k_max_levels] = {};
};

// -----------------------------------------------------------------------------
// Label Structures (Qt-free versions)
// -----------------------------------------------------------------------------

/// Vertical axis label
struct v_label_t
{
    double      value;      ///< The numeric value this label represents
    float       y;          ///< Y position in pixels
    std::string text;       ///< Label text (UTF-8)
};

/// Horizontal axis label
struct h_label_t
{
    double      value;      ///< The numeric value (timestamp) this label represents
    glm::vec2   position;   ///< Position in pixels
    std::string text;       ///< Label text (UTF-8)
};

// -----------------------------------------------------------------------------
// Layout Result
// -----------------------------------------------------------------------------

/// Result of a frame layout calculation
struct frame_layout_result_t
{
    double usable_width            = 0.0;
    double usable_height           = 0.0;
    double v_bar_width             = 0.0;
    double h_bar_height            = 0.0;  // Horizontal bar height (for labels)
    float  max_v_label_text_width  = 0.f;

    std::vector<h_label_t> h_labels;
    std::vector<v_label_t> v_labels;

    int    v_label_fixed_digits   = 0;
    bool   h_labels_subsecond     = false;

    // Seed hints for incremental computation
    int    vertical_seed_index    = -1;
    double vertical_seed_step     = 0.0;
    double vertical_finest_step   = 0.0;
    int    horizontal_seed_index  = -1;
    double horizontal_seed_step   = 0.0;
};

// -----------------------------------------------------------------------------
// Layout Cache Key
// -----------------------------------------------------------------------------

/// Key for caching layout results
struct layout_cache_key_t
{
    float     v0                           = 0.0f;
    float     v1                           = 0.0f;
    double    t0                           = 0.0;
    double    t1                           = 0.0;
    Size2i    viewport_size;
    double    adjusted_reserved_height     = 0.0;
    double    adjusted_preview_height      = 0.0;
    double    adjusted_font_size_in_pixels = 0.0;
    double    vbar_width_pixels            = 0.0;
    uint64_t  font_metrics_key             = 0;

    [[nodiscard]] bool operator==(const layout_cache_key_t& other) const noexcept
    {
        return v0 == other.v0 &&
               v1 == other.v1 &&
               t0 == other.t0 &&
               t1 == other.t1 &&
               viewport_size == other.viewport_size &&
               adjusted_reserved_height == other.adjusted_reserved_height &&
               adjusted_preview_height == other.adjusted_preview_height &&
               adjusted_font_size_in_pixels == other.adjusted_font_size_in_pixels &&
               vbar_width_pixels == other.vbar_width_pixels &&
               font_metrics_key == other.font_metrics_key;
    }

    [[nodiscard]] bool operator!=(const layout_cache_key_t& other) const noexcept
    {
        return !(*this == other);
    }
};

// -----------------------------------------------------------------------------
// Layout Cache
// -----------------------------------------------------------------------------

/// Caches layout results to avoid recomputation when inputs haven't changed.
class Layout_cache
{
public:
    using key_t = layout_cache_key_t;
    using value_type = frame_layout_result_t;

    [[nodiscard]] const value_type* try_get(const key_t& query) const noexcept
    {
        if (!m_valid || m_key != query) {
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

} // namespace vnm::plot::core
