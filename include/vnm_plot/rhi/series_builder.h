#pragma once

// VNM Plot Library - RHI Series Builder
// Convenience builder for series_data_t values with QRhi custom layers.

#include <vnm_plot/core/access_policy.h>
#include <vnm_plot/rhi/series_data.h>

namespace vnm::plot {

class Rhi_series_builder
{
public:
    Rhi_series_builder() = default;

    Rhi_series_builder& enabled(bool v)
    {
        m_series.enabled = v;
        return *this;
    }

    Rhi_series_builder& style(Display_style s)
    {
        m_series.style = s;
        return *this;
    }

    Rhi_series_builder& interpolation(Series_interpolation mode)
    {
        m_series.interpolation = mode;
        return *this;
    }

    Rhi_series_builder& empty_window_behavior(Empty_window_behavior behavior)
    {
        m_series.empty_window_behavior = behavior;
        return *this;
    }

    Rhi_series_builder& color(const glm::vec4& c)
    {
        m_series.color = c;
        return *this;
    }

    Rhi_series_builder& series_label(std::string label)
    {
        m_series.series_label = std::move(label);
        return *this;
    }

    Rhi_series_builder& data_source(std::shared_ptr<Data_source> source)
    {
        m_series.set_data_source(std::move(source));
        return *this;
    }

    Rhi_series_builder& data_source_ref(Data_source& source)
    {
        m_series.set_data_source_ref(source);
        return *this;
    }

    Rhi_series_builder& access(const Data_access_policy& policy)
    {
        m_series.access = policy;
        return *this;
    }

    template<typename Sample>
    Rhi_series_builder& access(const Data_access_policy_typed<Sample>& policy)
    {
        m_series.access = policy.erase();
        return *this;
    }

    Rhi_series_builder& preview(const preview_config_t& config)
    {
        m_series.preview_config = config;
        return *this;
    }

    Rhi_series_builder& qrhi_layer(
        std::shared_ptr<const Qrhi_series_layer> layer)
    {
        m_series.qrhi_layers.push_back(std::move(layer));
        return *this;
    }

    Rhi_series_builder& qrhi_layers(qrhi_series_layer_list_t layers)
    {
        m_series.qrhi_layers = std::move(layers);
        return *this;
    }

    Rhi_series_builder& clear_qrhi_layers()
    {
        m_series.qrhi_layers.clear();
        return *this;
    }

    rhi_series_data_t build_value() const { return m_series; }

    std::shared_ptr<rhi_series_data_t> build_shared() const
    {
        return std::make_shared<rhi_series_data_t>(m_series);
    }

private:
    rhi_series_data_t m_series;
};

} // namespace vnm::plot
