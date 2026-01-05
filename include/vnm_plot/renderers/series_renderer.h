#pragma once

// VNM Plot Library - Series Data
// Data descriptor used by Plot_widget to render series.
//
// This header re-exports series_data_t from data_source.h for backward
// compatibility with code that includes this header directly.

#include "../data_source.h"

// series_data_t is now defined in core/data_types.h and re-exported via
// data_source.h as vnm::plot::series_data_t. This header exists for backward
// compatibility with existing includes.
