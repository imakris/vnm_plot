#pragma once

#include <vnm_plot/core/series_window.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace vnm::plot::detail {

struct render_stack_source_feedback_t
{
    int                    series_id     = 0;
    const Data_source*     source        = nullptr;
    std::size_t            lod           = 0;
    std::uint64_t          sequence      = 0;
    Series_interpolation   interpolation = Series_interpolation::LINEAR;
    data_snapshot_t        cumulative;
};

struct render_stack_status_feedback_t
{
    Stack_view_status      status;
    std::vector<render_stack_source_feedback_t>
                           sources;
};

struct plot_render_feedback_t
{
    double                 measured_vbar_width = 0.0;
    float                  v_min                = 0.0f;
    float                  v_max                = 1.0f;
    std::int64_t           t_min                = 0;
    std::int64_t           t_max                = 1;
    std::int64_t           t_available_min      = 0;
    std::int64_t           t_available_max      = 1;
    std::uint64_t          series_revision      = 0;
    std::uint64_t          generation           = 0;
    std::map<int, std::vector<render_stack_source_feedback_t>>
                           stack_validity;
    std::map<std::pair<int, Series_view_kind>, render_stack_status_feedback_t>
                           stack_statuses;
    bool                   stack_validity_ready = false;
};

// Shared by the scene-graph renderer and its item without either retaining
// the other. Render publishes complete generations; the GUI thread consumes
// the newest generation from a context-bound, queued callback.
class plot_render_feedback_channel_t
{
public:
    void publish(plot_render_feedback_t feedback)
    {
        std::lock_guard lock(m_mutex);
        feedback.generation = ++m_generation;
        m_latest            = std::move(feedback);
    }

    std::optional<plot_render_feedback_t> take_after(std::uint64_t generation)
    {
        std::lock_guard lock(m_mutex);
        if (!m_latest || m_latest->generation <= generation) {
            return std::nullopt;
        }

        auto feedback = std::move(m_latest);
        m_latest.reset();
        return feedback;
    }

private:
    std::mutex                            m_mutex;
    std::optional<plot_render_feedback_t> m_latest;
    std::uint64_t                         m_generation = 0;
};

} // namespace vnm::plot::detail
