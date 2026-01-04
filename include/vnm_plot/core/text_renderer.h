#pragma once

// VNM Plot Library - Core Text Renderer
// Qt-free text rendering for axis labels and info overlay with fade animations.

#include "render_types.h"

#include <chrono>
#include <map>
#include <string>

namespace vnm::plot::core {

class Font_renderer;

// -----------------------------------------------------------------------------
// Text Renderer
// -----------------------------------------------------------------------------
// Renders vertical and horizontal axis labels with optional fade animations.
class Text_renderer
{
public:
    explicit Text_renderer(Font_renderer* fr);

    // Returns true while label fade animations are in progress.
    bool render(const frame_context_t& ctx, bool fade_v_labels, bool fade_h_labels);

    struct label_fade_state_t
    {
        float alpha = 0.0f;
        int direction = 0; // +1 fade-in, -1 fade-out, 0 steady
        std::string text;
    };

    struct axis_fade_tracker_t
    {
        std::map<double, label_fade_state_t> states;
        std::chrono::steady_clock::time_point last_update{};
        bool initialized = false;
    };

private:
    [[maybe_unused]] Font_renderer* m_fonts = nullptr;

    // Cached timestamps to avoid repeated allocation/formatting
    [[maybe_unused]] double m_last_t0 = -1.0;
    [[maybe_unused]] double m_last_t1 = -1.0;
    [[maybe_unused]] bool m_last_subsecond = false;
    std::string m_cached_from_ts;
    std::string m_cached_to_ts;

    static constexpr float k_label_fade_duration_ms = 250.0f;

    axis_fade_tracker_t m_vertical_fade;
    axis_fade_tracker_t m_horizontal_fade;

    bool render_axis_labels(const frame_context_t& ctx, bool fade_labels);
    bool render_info_overlay(const frame_context_t& ctx, bool fade_labels);
};

} // namespace vnm::plot::core
