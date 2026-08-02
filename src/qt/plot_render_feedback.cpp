#include "plot_render_feedback.h"

#include <vnm_plot/rhi/series_renderer.h>

namespace vnm::plot::detail {

void fill_stack_feedback(const Series_renderer& series, plot_render_feedback_t& feedback)
{
    for (const auto& [group, revisions] : series.main_stack_validity()) {
        auto& stored = feedback.stack_validity[group];
        stored.reserve(revisions.size());
        for (const auto& revision : revisions) {
            stored.push_back({
                revision.series_id,
                revision.source,
                revision.lod,
                revision.sequence,
                revision.interpolation,
                revision.cumulative});
        }
    }
    for (const auto& [key, rendered] : series.stack_view_statuses()) {
        auto& stored  = feedback.stack_statuses[key];
        stored.status = rendered.status;
        stored.sources.reserve(rendered.sources.size());
        for (const auto& source : rendered.sources) {
            stored.sources.push_back({
                source.series_id,
                source.source,
                source.lod,
                source.sequence,
                source.interpolation,
                source.cumulative});
        }
    }
    feedback.stack_validity_ready = true;
}

} // namespace vnm::plot::detail
