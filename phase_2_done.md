# Phase 2 Implementation Summary

## What Was Done

### Type Unification
Phase 2 unified the parallel type systems between `vnm::plot::core` and `vnm::plot` namespaces.

**Core types are now canonical. Wrapper headers re-export them:**

| Core Type | Wrapper Re-export |
|-----------|-------------------|
| `core::Display_style` | `vnm::plot::Display_style` |
| `core::shader_set_t` | `vnm::plot::shader_set_t` |
| `core::colormap_config_t` | `vnm::plot::colormap_config_t` |
| `core::data_snapshot_t` | `vnm::plot::data_snapshot_t` |
| `core::snapshot_result_t` | `vnm::plot::snapshot_result_t` |
| `core::Data_source` | `vnm::plot::Data_source` |
| `core::Data_access_policy` | `vnm::plot::Data_access_policy` |
| `core::series_data_t` | `vnm::plot::series_data_t` |

### Files Modified

| File | Changes |
|------|---------|
| `include/vnm_plot/plot_types.h` | Removed duplicate `Display_style`, `shader_set_t`, `colormap_config_t`; now re-exports from core |
| `include/vnm_plot/data_source.h` | Removed duplicate types; now re-exports `data_snapshot_t`, `snapshot_result_t`, `Data_source`, `Data_access_policy`, `series_data_t` from core |
| `include/vnm_plot/renderers/series_renderer.h` | Simplified to just include `data_source.h` (backward compat header) |
| `src/plot_renderer.cpp` | Removed `Data_source_adapter`, `to_core_style`, `to_core_status`, `to_core_snapshot`; simplified series rendering to use unified types directly |
| `src/plot_widget.cpp` | Updated callback existence checks to use `series->access.get_*` |
| `examples/function_plotter/function_plotter.cpp` | Updated to use new `series->access.*` and `series->shaders` field names |
| `examples/hello_plot/src/plot_controller.cpp` | Updated to use new `series->access.*` field names |

### Code Removed (~100 lines)
- `Data_source_adapter` class (43 lines)
- `to_core_style()` function
- `to_core_status()` function
- `to_core_snapshot()` function
- `core_source_cache` member and its cleanup loop

### API Changes (Breaking)
The `series_data_t` struct layout changed:

**Old (wrapper):**
```cpp
struct series_data_t {
    std::function<double(const void*)> get_timestamp;  // Top-level
    std::function<float(const void*)> get_value;       // Top-level
    std::map<Display_style, shader_set_t> shader_sets; // Note: shader_sets
    uint64_t layout_key;                               // Top-level
    std::function<void()> setup_vertex_attributes;     // Top-level
};
```

**New (core):**
```cpp
struct series_data_t {
    Data_access_policy access;  // Callbacks inside access
    std::map<Display_style, shader_set_t> shaders;  // Note: shaders (not shader_sets)

    // Convenience methods that delegate to access:
    double get_timestamp(const void* sample) const;
    float get_value(const void* sample) const;
    // ...
};
```

**Migration:**
- `series->get_timestamp = [...]` → `series->access.get_timestamp = [...]`
- `series->shader_sets[...]` → `series->shaders[...]`
- `series->layout_key` → `series->access.layout_key`
- `series->setup_vertex_attributes` → `series->access.setup_vertex_attributes`
- Callback existence checks: `if (series->get_timestamp)` → `if (series->access.get_timestamp)`
- Callback calls: `series->get_timestamp(sample)` still works (convenience method)

## Observations

### What Worked Well
1. **Clean separation**: Core types are truly Qt-free and self-contained
2. **Minimal adapter code**: The `Data_source_adapter` was only needed because of type duplication
3. **Template helper**: `compute_lod_scales<T>` in `core/algo.h` works with both layers seamlessly

### Potential Issues
1. **Qt builds not tested**: Only core library was built (Qt not available on this machine). The Qt wrapper and example code changes need verification on a Qt-enabled system.

2. **`Display_style` operator& semantics**: Core's `operator&` returns `Display_style` (the old wrapper returned `bool`). Returning `Display_style` is the correct design because: (a) it's semantically correct for bitwise AND, (b) it preserves the masked value for chaining or storage, (c) the codebase already uses the `!!` pattern (e.g., `if (!!(style & Display_style::LINE))`) for boolean tests.

3. **Cache digest change** (from Phase 1): Font cache will regenerate once due to glyph ordering change in `glyph_seed_string()`.

### Recommendations for Phase 3
1. **Unify LOD selection algorithm**: `choose_level_from_base_pps` exists in both renderers with different logic. The core's target-based approach (2 pps with 50% hysteresis) is cleaner and should become canonical.

2. **Consolidate algo headers**: `plot_algo.h` and `core/algo.h` have overlapping functions. Consider moving all to core and re-exporting.

3. **Verify Qt builds**: Run full build with Qt enabled to catch any remaining issues.

## Build Status
- Core library (`vnm_plot_core`): **BUILDS SUCCESSFULLY**
- Qt wrapper (`vnm_plot`): Not built (Qt not available)
- Examples: Not built (Qt not available)
