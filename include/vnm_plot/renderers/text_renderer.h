#pragma once

// VNM Plot Library - Text Renderer
// Renders axis labels and info overlay text with fade animations.

#include "../render_types.h"

#include <QByteArray>

#include <chrono>
#include <map>

namespace vnm::plot {

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
        QByteArray text;
    };

    struct axis_fade_tracker_t
    {
        std::map<double, label_fade_state_t> states;
        std::chrono::steady_clock::time_point last_update{};
        bool initialized = false;
    };

private:
    Font_renderer* m_fonts = nullptr;

    // Cached timestamps to avoid repeated allocation/formatting
    double m_last_t0 = -1.0;
    double m_last_t1 = -1.0;
    bool m_last_subsecond = false;
    QByteArray m_cached_from_ts;
    QByteArray m_cached_to_ts;

    static constexpr float k_label_fade_duration_ms = 250.0f;

    axis_fade_tracker_t m_vertical_fade;
    axis_fade_tracker_t m_horizontal_fade;

    bool render_axis_labels(const frame_context_t& ctx, bool fade_labels);
    bool render_info_overlay(const frame_context_t& ctx, bool fade_labels);
};

} // namespace vnm::plot
