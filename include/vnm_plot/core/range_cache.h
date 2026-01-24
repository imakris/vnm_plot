#pragma once

#include "plot_config.h"
#include "types.h"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vnm::plot {

struct lod_minmax_cache_t
{
    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    uint64_t sequence = 0;
    bool valid = false;
};

struct series_minmax_cache_t
{
    const void* identity = nullptr;
    std::vector<lod_minmax_cache_t> lods;
    uint64_t query_sequence = 0;
    bool query_sequence_valid = false;
};

inline bool validate_range_cache_sequences(
    const std::map<int, std::shared_ptr<series_data_t>>& series_map,
    std::unordered_map<int, series_minmax_cache_t>& cache_map,
    Auto_v_range_mode auto_mode)
{
    for (const auto& [id, series] : series_map) {
        if (!series || !series->enabled || !series->data_source) {
            continue;
        }
        if (!series->access.get_value && !series->access.get_range) {
            continue;
        }
        const std::size_t levels = series->data_source->lod_levels();
        if (levels == 0) {
            continue;
        }
        const std::size_t check_level =
            (auto_mode == Auto_v_range_mode::GLOBAL_LOD) ? (levels - 1) : 0;
        uint64_t sequence = series->data_source->current_sequence(check_level);
        if (sequence == 0) {
            auto snapshot_result = series->data_source->try_snapshot(check_level);
            if (!snapshot_result) {
                return false;
            }
            sequence = snapshot_result.snapshot.sequence;
        }
        series_minmax_cache_t& cache = cache_map[id];
        const void* identity = series->data_source->identity();
        if (cache.identity != identity || cache.lods.size() != levels) {
            return false;
        }
        if (auto_mode == Auto_v_range_mode::VISIBLE && cache.query_sequence_valid) {
            if (cache.query_sequence != sequence) {
                return false;
            }
            continue;
        }
        const auto& entry = cache.lods[check_level];
        if (!entry.valid || entry.sequence != sequence) {
            return false;
        }
    }
    return true;
}

} // namespace vnm::plot
