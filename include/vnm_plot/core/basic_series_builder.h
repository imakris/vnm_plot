#pragma once

// VNM Plot Library - Shared Series Builder Base
// CRTP helper for core and RHI series builders.

#include "access_policy.h"
#include "types.h"

#include <memory>
#include <string>
#include <utility>

namespace vnm::plot::detail {

template<typename Derived, typename Series>
class Basic_series_builder
{
public:
    Derived& enabled(bool v)
    {
        m_series.enabled = v;
        return derived();
    }

    Derived& style(Display_style s)
    {
        m_series.style = s;
        return derived();
    }

    Derived& interpolation(Series_interpolation mode)
    {
        m_series.interpolation = mode;
        return derived();
    }

    Derived& empty_window_behavior(Empty_window_behavior behavior)
    {
        m_series.empty_window_behavior = behavior;
        return derived();
    }

    Derived& nonfinite_policy(Nonfinite_sample_policy policy)
    {
        m_series.nonfinite_policy = policy;
        return derived();
    }

    Derived& color(const glm::vec4& c)
    {
        m_series.color = c;
        return derived();
    }

    Derived& series_label(std::string label)
    {
        m_series.series_label = std::move(label);
        return derived();
    }

    /// Join a cumulative stack. Zero disables stacking; equal non-zero values
    /// form a group whose bottom-to-top order is the ascending plot-ID order.
    Derived& stack_group(int group)
    {
        m_series.stack_group = group;
        return derived();
    }

    Derived& data_source(std::shared_ptr<Data_source> source)
    {
        m_series.set_data_source(std::move(source));
        return derived();
    }

    Derived& data_source_ref(Data_source& source)
    {
        m_series.set_data_source_ref(source);
        return derived();
    }

    Derived& access(const Data_access_policy& policy)
    {
        m_series.access = policy;
        return derived();
    }

    template<typename Sample>
    Derived& access(const Data_access_policy_typed<Sample>& policy)
    {
        m_series.access = policy.erase();
        return derived();
    }

    Derived& preview(const preview_config_t& config)
    {
        m_series.preview_config = config;
        return derived();
    }

    Series build_value() const { return m_series; }

    std::shared_ptr<Series> build_shared() const
    {
        return std::make_shared<Series>(m_series);
    }

protected:
    Series& mutable_series() noexcept { return m_series; }
    const Series& series() const noexcept { return m_series; }

private:
    Derived& derived() noexcept
    {
        return static_cast<Derived&>(*this);
    }

    Series m_series;
};

} // namespace vnm::plot::detail
