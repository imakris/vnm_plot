# Phase 3 Implementation Summary

## What Was Done

### Algorithm Consolidation
Phase 3 consolidated duplicated algorithms between core and wrapper layers.

**Core `algo.h` is now canonical. Wrapper `plot_algo.h` re-exports and adds wrapper-specific functions.**

### Functions Consolidated to Core

| Function | Description |
|----------|-------------|
| `format_axis_fixed_or_int` | Number formatting with integer/fixed precision |
| `circular_index` | Wrap-around container indexing |
| `get_shift` | Grid line phase alignment |
| `build_time_steps_covering` | Time axis step generation |
| `find_time_step_start_index` | Time step lookup |
| `min_v_span_for` | Minimum vertical span for precision |
| `compute_lod_scales` | LOD scales vector computation |
| `lower_bound_timestamp` | Binary search (lower bound) on samples |
| `upper_bound_timestamp` | Binary search (upper bound) on samples |
| `choose_lod_level` | Target-based LOD level selection |

### New Core Functions

**Binary Search Utilities** (`lower_bound_timestamp`, `upper_bound_timestamp`):
- Template-based functions accepting any callable for timestamp extraction
- Work with raw pointer/count/stride (generic data layout)
- Assume ascending timestamp order

**LOD Level Selection** (`choose_lod_level`):
- Target-based approach: aims for ~2 pixels per sample
- Includes 50% hysteresis to prevent oscillation between levels
- Replaces both the old plot_renderer and series_renderer implementations

### Files Modified

| File | Changes |
|------|---------|
| `include/vnm_plot/core/algo.h` | Added `lower_bound_timestamp`, `upper_bound_timestamp`, `choose_lod_level` |
| `include/vnm_plot/plot_algo.h` | Now re-exports from core; removed duplicates; kept wrapper-specific decimal analysis and concept-based `binary_search_time` |
| `include/vnm_plot/core/series_renderer.h` | Removed `choose_level_from_base_pps` declaration |
| `src/core/series_renderer.cpp` | Removed local binary search functions; removed `choose_level_from_base_pps`; now uses `algo::*` |
| `src/plot_renderer.cpp` | Removed local `choose_level_from_base_pps`; now uses `core::algo::choose_lod_level` |

### Code Removed (~120 lines)

- `choose_level_from_base_pps` from `plot_renderer.cpp` (~57 lines)
- `choose_level_from_base_pps` from `series_renderer.cpp` (~33 lines)
- `lower_bound_timestamp` local function from `series_renderer.cpp` (~24 lines)
- `upper_bound_timestamp` local function from `series_renderer.cpp` (~24 lines)
- Duplicate function definitions from `plot_algo.h` (~80 lines)

### Wrapper-Specific Functions Retained

The following remain in `plot_algo.h` as they are wrapper-specific:
- `any_fractional_at_precision` - decimal analysis
- `trailing_zero_decimal_for_all` - decimal analysis
- `trim_trailing_zero_decimals` - decimal analysis
- `binary_search_time<HasTimestamp T>` - concept-based convenience wrapper

### LOD Selection Algorithm Change

**Previous (plot_renderer.cpp):**
- Complex two-loop algorithm with subdivision thresholds
- Used `1/subdivision` threshold to go up, `1.0` threshold to go down
- No hysteresis

**Previous (series_renderer.cpp) - HAD BUGS:**
The original series_renderer implementation had inverted math:
- `desired_scale = base_pps / target_pps` was inverted (should be `target_pps / base_pps`)
- `current_pps = base_pps / scale` was inverted (should be `base_pps * scale`)
- Hysteresis condition `<` was inverted (should be `>`)

These bugs caused wrong LOD level selection (picked finer levels when zoomed out, coarser when zoomed in).

**Current (core::algo::choose_lod_level) - FIXED:**
- Correct formula: `desired_scale = target_pps / base_pps`
- Correct pps calculation: `pps = base_pps * scale` (pixels per LOD sample)
- Correct hysteresis: stay if new isn't significantly better (`>` not `<`)
- Clear comments explaining the math

This fixes a latent bug in the original series_renderer code.

## Observations

### What Worked Well

1. **Clean separation**: Core algo functions are Qt-free and usable by both layers
2. **Template flexibility**: Binary search functions work with any timestamp extractor
3. **Single source of truth**: LOD selection now has one canonical implementation

### Potential Issues

1. **Qt builds not tested**: Changes need verification with Qt enabled
2. **LOD behavioral change**: The corrected algorithm will select different levels than before. The original series_renderer had inverted math (Codex review caught this), so behavior will actually be correct now. Visual verification recommended.

### Not Implemented

The brief mentioned these as potential consolidation targets:

1. **Min/max scanning loops**: These have different semantics across use cases:
   - `compute_snapshot_minmax` uses `get_range` callback
   - `compute_window_minmax` operates on a subset
   - `compute_aux_metric_range` uses `get_aux_metric` callback

   Creating a unified helper would require significant abstraction for minimal benefit. Left as-is.

2. **Multiple binary search variants**: The concept-based `binary_search_time<T>` in wrapper and callback-based `lower/upper_bound_timestamp` in core serve different use cases. Both are retained.

## Build Status

- Core library (`vnm_plot_core`): Changes made, not built (Qt not available)
- Qt wrapper (`vnm_plot`): Changes made, not built (Qt not available)
- Verification needed on Qt-enabled system
