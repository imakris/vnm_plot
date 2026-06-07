#pragma once

// VNM Plot Library - RHI Text Renderer
// Qt-free text rendering for axis labels and info overlay with fade animations.

#include <vnm_plot/rhi/frame_context.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

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

    // QRhi path: build all text draw batches and upload resources before beginPass().
    bool prepare(const frame_context_t& ctx, bool fade_v_labels, bool fade_h_labels);

    // QRhi path: record the prepared text draw calls inside the active pass.
    void record(const frame_context_t& ctx);

    struct label_fade_state_t
    {
        float alpha = 0.0f;
        int direction = 0; // +1 fade-in, -1 fade-out, 0 steady
        std::string text;
    };

    template<typename Key>
    struct axis_fade_tracker_t
    {
        using key_type = Key;

        std::map<Key, label_fade_state_t> states;
        std::chrono::steady_clock::time_point last_update{};
        bool initialized = false;
    };

    using vertical_axis_fade_tracker_t = axis_fade_tracker_t<double>;
    using horizontal_axis_fade_tracker_t = axis_fade_tracker_t<std::int64_t>;

private:
    Font_renderer* m_fonts = nullptr;

    // Cached timestamps to avoid repeated allocation/formatting (int64 ns).
    static constexpr std::int64_t k_invalid_cached_ts = std::numeric_limits<std::int64_t>::min();
    std::int64_t m_last_t0 = k_invalid_cached_ts;
    std::int64_t m_last_t1 = k_invalid_cached_ts;
    std::uint64_t m_last_timestamp_revision = 0;
    bool m_last_subsecond = false;
    std::string m_cached_from_ts;
    std::string m_cached_to_ts;

    static constexpr float k_label_fade_duration_ms = 250.0f;

    vertical_axis_fade_tracker_t m_vertical_fade;
    horizontal_axis_fade_tracker_t m_horizontal_fade;

    bool render_axis_labels(const frame_context_t& ctx, bool fade_labels);
    bool render_info_overlay(const frame_context_t& ctx, bool fade_labels);
};

} // namespace vnm::plot
