#pragma once

#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/types.h>

#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace vnm::plot::detail {

struct auto_range_cache_entry_t
{
    const void*                source_identity        = nullptr;
    const Data_access_policy*  access_identity        = nullptr;
    access_policy_cache_key_t  access_key;
    std::uint64_t              layout_key             = 0;
    std::uint64_t              semantics_value        = 0;
    std::uint64_t              semantics_revision     = 0;
    bool                       semantics_conservative = true;
    std::size_t                lod_level              = 0;
    std::int64_t               t_min_ns               = 0;
    std::int64_t               t_max_ns               = 0;
    Series_interpolation       interpolation          = Series_interpolation::LINEAR;
    Empty_window_behavior      empty_window_behavior  = Empty_window_behavior::DRAW_NOTHING;
    Nonfinite_sample_policy    nonfinite_policy       = Nonfinite_sample_policy::BREAK_SEGMENT;
    std::uint64_t              sequence               = 0;
    value_range_t              range{};
    Data_query_status          status                 = Data_query_status::EMPTY;
    bool                       valid                  = false;
};

struct auto_range_cache_t
{
    std::map<int, auto_range_cache_entry_t>    main_entries;
    std::map<int, auto_range_cache_entry_t>    preview_entries;
};

std::pair<float, float> resolve_main_v_range(
    const std::map<int, std::shared_ptr<const series_data_t>>& series,
    const data_config_t&                                       data_cfg,
    const Plot_config&                                         config,
    bool                                                       v_auto,
    auto_range_cache_t*                                        cache = nullptr);

std::pair<float, float> resolve_preview_v_range(
    const std::map<int, std::shared_ptr<const series_data_t>>& series,
    const data_config_t&                                       data_cfg,
    const Plot_config&                                         config,
    auto_range_cache_t*                                        cache = nullptr);

} // namespace vnm::plot::detail
