#pragma once

// VNM Plot Library - RHI Series Data Extensions
// QRhi custom-layer storage layered on top of core series_data_t.

#include <vnm_plot/core/types.h>

#include <memory>
#include <vector>

namespace vnm::plot {

class Qrhi_series_layer;

using qrhi_series_layer_list_t =
    std::vector<std::shared_ptr<const Qrhi_series_layer>>;

struct rhi_series_data_t : series_data_t
{
    qrhi_series_layer_list_t qrhi_layers;

    [[nodiscard]] std::shared_ptr<series_data_t> clone() const override
    {
        return std::make_shared<rhi_series_data_t>(*this);
    }
};

inline const qrhi_series_layer_list_t& qrhi_layers_for(
    const series_data_t& series) noexcept
{
    static const qrhi_series_layer_list_t k_empty_layers;
    const auto* rhi_series = dynamic_cast<const rhi_series_data_t*>(&series);
    return rhi_series ? rhi_series->qrhi_layers : k_empty_layers;
}

} // namespace vnm::plot
