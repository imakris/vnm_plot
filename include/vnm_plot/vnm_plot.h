#pragma once

// VNM Plot Library - Main Header
// A high-performance, GPU-accelerated plotting library.
//
// This library provides:
// - Generic data source interface (Data_source)
// - Support for streaming data with LOD (Level of Detail)
// - GPU-accelerated rendering via OpenGL
// - Qt Quick integration for interactive plots
//
// Usage:
//   1. Create a data source (Vector_data_source, or implement Data_source)
//   2. Create a Data_access_policy for your sample type
//   3. Create a VNM_plot widget (Qt Quick) and add data sources
//
// For simple function plotting:
//   #include <vnm_plot/function_sample.h>
//   auto source = std::make_shared<vnm::plot::Function_data_source>();
//   source->generate([](double x) { return std::sin(x); }, 0, 10, 1000);

#include "data_source.h"
#include "plot_types.h"
#include "plot_algo.h"
#include "function_sample.h"

namespace vnm::plot {

// Library version
constexpr int k_version_major = 0;
constexpr int k_version_minor = 1;
constexpr int k_version_patch = 0;

constexpr const char* k_version_string = "0.1.0";

} // namespace vnm::plot
