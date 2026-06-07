#pragma once

// VNM Plot Library - RHI Series Builder
// Convenience builder for series_data_t values with QRhi custom layers.

#include <vnm_plot/core/basic_series_builder.h>
#include <vnm_plot/rhi/series_data.h>

namespace vnm::plot {

class Rhi_series_builder
    : public detail::Basic_series_builder<Rhi_series_builder, rhi_series_data_t>
{
public:
    Rhi_series_builder() = default;

    Rhi_series_builder& qrhi_layer(
        std::shared_ptr<const Qrhi_series_layer> layer)
    {
        mutable_series().qrhi_layers.push_back(std::move(layer));
        return *this;
    }

    Rhi_series_builder& qrhi_layers(qrhi_series_layer_list_t layers)
    {
        mutable_series().qrhi_layers = std::move(layers);
        return *this;
    }

    Rhi_series_builder& clear_qrhi_layers()
    {
        mutable_series().qrhi_layers.clear();
        return *this;
    }
};

} // namespace vnm::plot
