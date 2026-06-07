#include "frame_range_planner.h"

#include <cmath>
#include <utility>

namespace vnm::plot::detail {

namespace {

value_range_plan_t make_value_range_plan(std::pair<float, float> range)
{
    value_range_plan_t plan;
    plan.min = range.first;
    plan.max = range.second;
    plan.valid = std::isfinite(plan.min) && std::isfinite(plan.max) && plan.min <= plan.max;
    return plan;
}

} // namespace

Frame_range_plan Frame_range_planner::plan(
    const std::map<int, std::shared_ptr<const series_data_t>>& series,
    const data_config_t& data_cfg,
    const Plot_config& config,
    bool v_auto,
    bool preview_enabled)
{
    Frame_range_plan plan;
    plan.main_v_range = make_value_range_plan(
        resolve_main_v_range(series, data_cfg, config, v_auto, &m_cache));

    if (preview_enabled) {
        plan.preview_v_range = make_value_range_plan(
            resolve_preview_v_range(series, data_cfg, config, &m_cache));
    }
    else {
        plan.preview_v_range = plan.main_v_range;
    }

    return plan;
}

} // namespace vnm::plot::detail
