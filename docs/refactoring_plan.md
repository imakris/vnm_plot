# Refactoring Plan: Multi-Source Preview + Shared Time Axis

**Date**: 2026-02-06

## Goals
- Allow each series to use a distinct preview data source and layout, as long as timestamps share the same type and axis.
- Allow preview and main views to use different `Display_style` values.
- Keep a shared time axis across multiple `Plot_widget` instances with both explicit `Plot_time_axis` binding and `attach_time_axis()` chaining.
- Ensure preview height `0` disables preview overhead and rendering.
- Preserve existing performance optimizations for the common case where preview and main are identical.

## Non-Goals
- No single-widget multi-subplot renderer in this phase.
- No changes to data source semantics beyond preview support.
- No preview-specific shader sets (deferred to future work if needed).

## Threading Model
- `Plot_widget` is accessed from both the GUI thread and the render thread. This plan must preserve the existing lock/atomic usage in `Plot_widget` and `Plot_renderer`.
- `Plot_time_axis` is a GUI-thread QObject. Access to it should occur on the GUI thread; `Plot_widget` should mirror its values into `m_data_cfg` under existing locks to keep the render thread safe.

## High-Level Approach
- Extend `series_data_t` with an optional `preview_config_t` struct for preview-specific configuration.
- Add a `Plot_time_axis` QObject to unify `t` range state across widgets.
- Introduce preview-specific range caches and reuse existing caches when preview matches main.
- Skip all preview work when preview height is `0` or `preview_visibility` is `0`.

## Data Model Changes

### Preview Configuration Struct
Add to `include/vnm_plot/core/types.h`:

```cpp
struct preview_config_t
{
    std::shared_ptr<Data_source> data_source;   // required when preview_config is set
    Data_access_policy access;                  // optional; if invalid, fall back to main access
    std::optional<Display_style> style;         // nullopt means use main style
};
```

### Series Data Extension
Update `series_data_t` to add:
```cpp
std::optional<preview_config_t> preview_config;
```

Add helper methods on `series_data_t` (inline, header-only):
```cpp
const Data_source* main_source() const;

// Returns preview source; if preview_config is set but data_source is null, preview is skipped (log).
const Data_source* preview_source() const;

const Data_access_policy& main_access() const;

// Returns preview access when preview_config is set and access is valid, else main access.
const Data_access_policy& preview_access() const;

// Returns preview style if set, else main style.
Display_style effective_preview_style() const;

// True if preview_config is set.
bool has_preview_config() const;

// True if preview uses same source pointer, same layout_key, and same effective style as main.
bool preview_matches_main() const;
```

### Layout Key Precomputation
`Data_access_policy` already has a `layout_key` member. This key should be computed once at policy construction (not per-frame) and used for VAO/cache identity.

### Data_access_policy Validity
`Data_access_policy` validity is determined by `access.is_valid()` (non-null `get_timestamp` and either `get_value` or `get_range`, plus `sample_stride > 0`). A default-constructed policy is considered invalid and will fall back to the main access policy.

## Renderer and Cache Changes

### Data Source Identity
Data source identity is **pointer-based** (`Data_source*` comparison). Two `shared_ptr` instances pointing to the same `Data_source` object are identical; two distinct objects with equivalent data are not. This is intentional: if you want cache sharing, share the source object.

### Range Caches
Update `src/qt/plot_renderer.cpp` range computations:
- Add preview-specific range functions that use `preview_source()` and `preview_access()`.
- Maintain a separate `preview_v_range_cache` in `impl_t::view_state_t`.
- Reuse main caches only when `preview_matches_main()` returns true.

### Hash Signature
Update `hash_data_sources(...)` in `src/qt/plot_renderer.cpp`:
- Include both main and preview data source pointers per series.
- Include `preview_config.has_value()` and preview `layout_key()` when present.
- Include preview effective style when preview differs from main.

### Range Cache Validation
Update `include/vnm_plot/core/range_cache.h`:
- Add `validate_preview_range_cache_sequences(...)` as a separate function for preview caches, mirroring the main cache validation but operating on preview sources.

### Cache Lifetime
The frame-scoped snapshot cache is cleared at the start of each frame. No explicit eviction policy is needed.

## Series Rendering Changes
Update `src/core/series_renderer.cpp`:
- For each series, check `has_preview_config()`:
  - If false, preview rendering uses main source/access/style (existing path).
  - If true, preview rendering uses `preview_source()`, `preview_access()`, `effective_preview_style()`.
- When `preview_matches_main()` is true, reuse the existing main view VBO/VAO path.
- When preview differs, maintain separate VBO view state for preview.
- Modify `ensure_series_vao(...)` signature to accept `const Data_access_policy&` instead of relying on `series.access`.

### Snapshot Cache Key
Update snapshot caching in `process_view(...)`:
- Key by `(Data_source*, applied_level)`.
- Preview can reuse main's cached snapshot only when source pointers match and applied_level matches.

## Preview V-Range Animation
Preview v-range animation should preserve current behavior unless explicitly changed. If preview uses a distinct v-range (due to different source/access), it animates toward its own target using the same smoothing parameters as the main view. Any change here is a user-visible behavior change and must be called out separately.

## Preview-Disabled Fast Path
Add `preview_enabled` boolean early in `Plot_renderer::render()`:
```cpp
const bool preview_enabled = (adjusted_preview_height > 0) && (config.preview_visibility > 0);
```
If false:
- Skip preview v-range computation.
- Skip preview rendering passes.
- Skip preview overlay rendering.

## Time Axis Synchronization

### Plot_time_axis QObject
Files: `include/vnm_plot/qt/plot_time_axis.h`, `src/qt/plot_time_axis.cpp`.

Properties (all read/write with notify signals):
- `double t_min`
- `double t_max`
- `double t_available_min`
- `double t_available_max`

Signals:
- `t_limits_changed()`

Note: This is a coarse signal that fires for any change to `t_min/t_max/t_available_*`. This is intentional and matches current `Plot_widget` behavior. Consumers should assume all `t` properties may have changed.

Methods:
- `void set_t_range(double t_min, double t_max)`
- `void set_available_t_range(double t_available_min, double t_available_max)`
- `void adjust_t_from_mouse_diff(double ref_width, double diff)`
- `void adjust_t_from_mouse_diff_on_preview(double ref_width, double diff)`
- `void adjust_t_from_mouse_pos_on_preview(double ref_width, double x_pos)`
- `void adjust_t_from_pivot_and_scale(double pivot, double scale)`
- `void adjust_t_to_target(double target_min, double target_max)`

Notes:
- The axis does not own animation state. `adjust_t_to_target` applies immediately; any animation is the caller's responsibility (e.g., the widget/interaction layer).
- `ref_width` is supplied by the calling widget; in multi-widget setups, the widget receiving the mouse event drives the axis.
- Preview mouse methods exist because preview spans the full `t_available` range, so the pixel-to-time mapping differs from the main view.

### Plot_widget Integration
Update `Plot_widget`:
- Add `Q_PROPERTY(Plot_time_axis* timeAxis READ time_axis WRITE set_time_axis NOTIFY time_axis_changed)`.
- Store `Plot_time_axis* m_time_axis = nullptr` (non-owning pointer).
- `set_time_axis(Plot_time_axis* axis)`:
  - Disconnect signals from previous axis if any.
  - Connect to new axis signals.
  - Emit `time_axis_changed()`.
- Add `Q_INVOKABLE void attach_time_axis(Plot_widget* other)`:
  - If `other` is null, log and return.
  - If `other->time_axis()` is null, log and return.
  - Otherwise call `set_time_axis(other->time_axis())`.
- All `adjust_t_*`, `set_t_range`, `set_available_t_range` methods forward to `m_time_axis` when set; otherwise operate on local state.

### Ownership Model
`Plot_time_axis` is **not owned** by `Plot_widget`. The axis must be created and owned externally (typically as a QML object or a parent QObject). When a `Plot_widget` is destroyed, it simply disconnects from the axis. When an axis is destroyed, all connected widgets receive the destroyed signal and clear their pointer (standard Qt parent-child or QPointer pattern).

Use `QPointer<Plot_time_axis>` for storage to handle axis destruction gracefully.

### QML Integration
Update `qml/VnmPlot/PlotView.qml`:
- Expose `property PlotTimeAxis timeAxis` (match the registered QML type name).
- Bind `plot.timeAxis: root.timeAxis` when provided.

## Error Handling
Maintain the current soft-failure policy (warnings/logs + skip) unless we explicitly choose to hard-fail. For preview config:
- If `preview_config` is set but `data_source` is null, log and skip preview rendering for that series.
- If preview access is invalid, fall back to main access and log once.
- If a preview data source has incompatible timestamp type with the main source, log and skip preview rendering for that series.

## Performance Preservation for Common Case
When `preview_matches_main()` returns true:
- Reuse main VBO/VAO and snapshot caches.
- Reuse main LOD scales.
- Reuse main v-range cache (no separate preview cache allocation).

This ensures that users who don't use distinct preview configurations pay no additional cost.

## Tests and Validation
Add tests that cover:
- Series with `preview_config` set uses distinct source/access/style without corrupting main view.
- Series without `preview_config` behaves identically to current implementation.
- `preview_matches_main()` correctly identifies matching vs differing configurations.
- Preview height `0` skips preview v-range and render passes (measure frame time or check call counts).
- Shared time axis updates all attached widgets synchronously.
- `attach_time_axis(other)` where `other->time_axis()` is null logs and returns without change.
- Destroying a `Plot_time_axis` while widgets are attached does not crash.
- Incompatible timestamp types between main and preview sources log and skip preview rendering.

Add a QML example demonstrating stacked plots sharing a single `Plot_time_axis`.

## Documentation
Update API documentation for:
- `series_data_t::preview_config` and related helpers.
- `Plot_time_axis` class and its properties/methods.
- `Plot_widget::timeAxis` property and `attach_time_axis()` method.
- Threading requirements (GUI thread for axis; widget/render thread synchronization via existing locks).

## Implementation Order
1. Add `Plot_time_axis` QObject and wire `Plot_widget` to it (orthogonal, can be validated independently).
2. Add `preview_config_t` struct and extend `series_data_t` with `preview_config` and helper methods.
3. Ensure `Data_access_policy::layout_key` is computed once at policy construction.
4. Update renderer caches, hash signatures, and v-range computations for preview.
5. Update series renderer to use per-view access/style and to optimize the common case.
6. Add preview-disabled fast path.
7. Add tests and example usage.
8. Update documentation.

## Design Decisions

### Per-Series vs Plot-Level Preview Style
**Decision**: Per-series configuration.

**Rationale**: Different series may legitimately need different preview representations (e.g., one series as LINE in preview, another as AREA). A plot-level default would require per-series overrides anyway, adding complexity without benefit. The `std::optional<Display_style>` in `preview_config_t` already defaults to main style when unset, which covers the common case.

### Preview-Specific Shader Sets
**Decision**: Deferred.

**Rationale**: Shaders are currently selected based on `Display_style`. Since preview can already use a different style, it implicitly gets the appropriate shader. Custom shader sets for preview (beyond what style selection provides) is an edge case that can be added later if needed without breaking changes.
