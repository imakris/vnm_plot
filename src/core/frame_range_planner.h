#pragma once

#include "auto_range_resolver.h"

#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/series_window.h>
#include <vnm_plot/core/types.h>

#include <map>
#include <memory>

namespace vnm::plot::detail {

class Frame_range_planner
{
public:
    Frame_range_plan plan(
        const std::map<int, std::shared_ptr<const series_data_t>>& series,
        const data_config_t& data_cfg,
        const Plot_config& config,
        bool v_auto,
        bool preview_enabled);

private:
    auto_range_cache_t m_cache;
};

} // namespace vnm::plot::detail
