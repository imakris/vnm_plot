#pragma once

// VNM Plot Library - Series Builder
// Convenience builder for constructing data/layout series_data_t instances.

#include "access_policy.h"
#include "types.h"

namespace vnm::plot {

class Series_builder
{
public:
    Series_builder() = default;

    Series_builder& enabled(bool v)
    {
        m_series.enabled = v;
        return *this;
    }

    Series_builder& style(Display_style s)
    {
        m_series.style = s;
        return *this;
    }

    Series_builder& interpolation(Series_interpolation mode)
    {
        m_series.interpolation = mode;
        return *this;
    }

    Series_builder& empty_window_behavior(Empty_window_behavior behavior)
    {
        m_series.empty_window_behavior = behavior;
        return *this;
    }

    Series_builder& color(const glm::vec4& c)
    {
        m_series.color = c;
        return *this;
    }

    Series_builder& series_label(std::string label)
    {
        m_series.series_label = std::move(label);
        return *this;
    }

    Series_builder& data_source(std::shared_ptr<Data_source> source)
    {
        m_series.set_data_source(std::move(source));
        return *this;
    }

    Series_builder& data_source_ref(Data_source& source)
    {
        m_series.set_data_source_ref(source);
        return *this;
    }

    Series_builder& access(const Data_access_policy& policy)
    {
        m_series.access = policy;
        return *this;
    }

    template<typename Sample>
    Series_builder& access(const Data_access_policy_typed<Sample>& policy)
    {
        m_series.access = policy.erase();
        return *this;
    }

    Series_builder& preview(const preview_config_t& config)
    {
        m_series.preview_config = config;
        return *this;
    }

    series_data_t build_value() const { return m_series; }

    std::shared_ptr<series_data_t> build_shared() const
    {
        return std::make_shared<series_data_t>(m_series);
    }

private:
    series_data_t m_series;
};

} // namespace vnm::plot
