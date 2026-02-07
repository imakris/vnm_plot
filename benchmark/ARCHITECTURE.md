# vnm_plot Synthetic Benchmark - Architecture and Implementation

## Purpose
This benchmark is a standalone Qt + OpenGL application that generates synthetic
time-series data and measures vnm_plot rendering performance. It mirrors the
profiling output used by Lumis so reports are directly comparable.

## High-Level Flow
1. A generator thread produces Bar or Trade samples using a Brownian motion
   model and writes into a ring buffer.
2. The render thread (Qt UI) snapshots the ring buffer through a vnm_plot
   Data_source and renders the series using vnm_plot core renderers.
3. A custom profiler collects nested vnm_plot scope timings and writes a
   Lumis-compatible report at the end of the run.

## Major Components
- Ring_buffer<T>
  - Thread-safe circular buffer with overwrite semantics.
  - Supports copy-on-snapshot via copy_to() for stable reads.

- Brownian_generator
  - Generates Bar_sample or Trade_sample using geometric Brownian motion.
  - Deterministic when seeded; timestamps advance at the configured rate.

- Benchmark_data_source<T>
  - Implements vnm::plot::Data_source.
  - Uses copy-on-snapshot to provide contiguous data for rendering.
  - Tracks sequence numbers and computes value ranges for auto-scaling.

- Benchmark_profiler
  - Implements vnm::plot::Profiler.
  - Aggregates scopes by name to match Lumis output.
  - Writes a fixed-width, hierarchical report with UTC timestamps.

- Benchmark_window (Qt OpenGL widget)
  - Owns renderers, asset loader, and series configuration.
  - Starts the generator thread and drives rendering at ~60 Hz.
  - Updates view ranges based on the latest data snapshot.

## Threading Model
- Generator thread:
  - Produces samples at the configured rate using steady_clock pacing.
  - Writes into the ring buffer only; no OpenGL work.

- UI/render thread:
  - Executes paintGL() via Qt's event loop.
  - Requests snapshots from the Data_source and renders with vnm_plot.
  - Records profiling scopes through Plot_config::profiler.

## Rendering Pipeline
- vnm_plot Asset_loader loads embedded shaders.
- Series_renderer draws the data using function_sample.vert plus:
  - Bars: plot_area.geom / plot_area.frag
  - Trades: plot_dot_quad.geom / plot_dot_quad.frag
- Chrome_renderer draws grids and preview overlays.
- Primitive_renderer flushes batched primitives.

## Data Access Policies
Bar_sample and Trade_sample are packed structs. Each Data_access_policy defines:
- Timestamp extraction (double)
- Primary value (close or price)
- Range (low/high for Bars, price/price for Trades)
- Optional aux metric (volume or size)
- Vertex attribute setup matching function_sample.vert
- A stable layout_key for VAO caching

## Profiling and Report Output
Benchmark_profiler produces a Lumis-compatible report:
- Fixed header fields (session, UTC timestamps, compiler)
- Sorted metadata keys
- Fixed-width table and tree glyphs
- Optional extended metadata behind --extended-metadata

The report is written at the end of the benchmark run to the configured output
folder with the inspector_benchmark_YYYYMMDD_HHMMSS_<SYMBOL>_<DataType>.txt
naming convention.

## Configuration and CLI
The executable accepts CLI options for duration, data type, seed, rate, ring
capacity, volatility, output directory, and report session/symbol. The seed is
resolved once at startup to keep runs reproducible.

## Key Files
- benchmark/include/ring_buffer.h
- benchmark/include/brownian_generator.h
- benchmark/include/benchmark_data_source.h
- benchmark/include/benchmark_profiler.h
- benchmark/include/benchmark_window.h
- benchmark/src/benchmark_window.cpp
- benchmark/src/main.cpp
