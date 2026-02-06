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
//   #include <vnm_plot/vnm_plot.h>
//   auto source = std::make_shared<vnm::plot::Function_data_source>();
//   source->generate([](double x) { return std::sin(x); }, 0, 10, 1000);
//
// This is the only public header; other headers are internal.
#include <vnm_plot/core/types.h>
#include <vnm_plot/core/plot_config.h>
#include <vnm_plot/core/function_sample.h>
#include <vnm_plot/core/algo.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/color_palette.h>
#include <vnm_plot/core/layout_calculator.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/gl_program.h>
#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/chrome_renderer.h>
#include <vnm_plot/core/series_renderer.h>
#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/text_renderer.h>

#if defined(VNM_PLOT_WITH_QT)
#include <vnm_plot/qt/plot_widget.h>
#include <vnm_plot/qt/plot_interaction_item.h>
#include <vnm_plot/qt/plot_time_axis.h>
#endif

namespace vnm::plot {

// Library version
constexpr int k_version_major = 0;
constexpr int k_version_minor = 1;
constexpr int k_version_patch = 0;

constexpr const char* k_version_string = "0.1.0";

} // namespace vnm::plot
