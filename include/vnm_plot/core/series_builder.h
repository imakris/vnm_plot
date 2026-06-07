#pragma once

// VNM Plot Library - Series Builder
// Convenience builder for constructing data/layout series_data_t instances.

#include "basic_series_builder.h"

namespace vnm::plot {

class Series_builder
    : public detail::Basic_series_builder<Series_builder, series_data_t>
{
public:
    Series_builder() = default;
};

} // namespace vnm::plot
