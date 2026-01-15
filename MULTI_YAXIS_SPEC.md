# Multi Y-Axis Support Specification

## 1. Problem Statement

When plotting multiple signals/data sources together, they currently share a single Y-axis with unified scaling. This causes issues when:
- Signals have vastly different scales (e.g., one ranges 0-10, another 0-10000)
- Correlated signals become unreadable because the lower-scale signal appears flat

**Current Workaround:** Manual scaling factors per source (limited and requires user intervention)

**Desired Solution:** Independent Y-axes per signal, each with its own scale and colored tick labels

---

## 2. Design Decisions (Finalized)

| Decision | Choice |
|----------|--------|
| Max axes | **N axes** (unlimited, one per series or axis group) |
| Axis grouping | **Yes** - multiple series can share an axis via axis_id |
| Grid behavior | **Tick marks only** on each axis bar, no full horizontal grid |
| V-bar width | **Per-axis computed** - each axis width from measured label width, total = sum |
| Trigger mode | **Explicit API** - user calls `set_series_axis()` |
| Multi-axis detection | Based on **visible/enabled** series only |
| Axis color | **Stable** - assigned on first axis creation, cached across visibility changes |
| Animation state | **Persistent** - survives across frames for smooth range transitions |

---

## 3. Proposed Feature Overview

### Core Concept
Each data source/signal can be assigned to an axis:
- **axis_id = 0**: Default axis (backward compatible single-axis mode)
- **axis_id > 0**: Independent axis (series with same axis_id share that axis)

Each axis has:
- Its own value range (v_min/v_max) computed from its visible member series
- Color-coded tick labels (color assigned on axis creation, stable across visibility changes)
- Small tick marks on the axis bar (no full horizontal grid)
- Own bar in the V-bar area (width computed from label measurements, bars side-by-side)

### Grid Behavior
When multiple axes are active (any **visible/enabled** series has axis_id > 0):
- **No horizontal grid lines** in plot area
- **Tick marks** on each axis bar (small horizontal lines at label positions)
- **Vertical grid lines** remain (time-based, shared by all signals)

### Series Visibility
A series "contributes" to rendering if it is enabled and has a valid data source. Only contributing series:
- Affect axis range computation
- Determine multi-axis vs single-axis mode
- Are rendered

---

## 4. Architecture Changes Required

### 4.1 Data Structures

**New/Modified Types** (`types.h`):

```cpp
// Extend series_data_t with axis assignment
struct series_data_t {
    // ... existing fields ...
    int axis_id = 0;  ///< 0 = default axis, >0 = independent axis group
};
```

**Persistent axis state** (in `plot_renderer::impl_t` or `plot_widget`):

```cpp
// Persistent state per axis - survives across frames for animation and color stability
struct axis_persistent_state_t {
    glm::vec4 color;                 ///< Assigned on axis creation, stable
    float anim_v_min = 0.0f;         ///< Current animated v_min
    float anim_v_max = 1.0f;         ///< Current animated v_max
};
std::unordered_map<int, axis_persistent_state_t> axis_persistent;  // keyed by axis_id
```

**Per-frame computed state** (rebuilt each frame):

```cpp
// Computed each frame from contributing series
struct axis_frame_state_t {
    int axis_id = 0;
    float target_v_min = 0.0f;       ///< Target for this frame (from series data)
    float target_v_max = 1.0f;
    std::vector<int> series_ids;     ///< Contributing series on this axis
};
```

**Layout Result Extension** (`layout_calculator.h`):

```cpp
struct axis_layout_t {
    int axis_id = 0;
    std::vector<v_label_t> labels;
    float x_offset = 0.0f;       ///< Horizontal offset from plot area right edge
    float bar_width = 0.0f;      ///< Computed from max label width + padding
    float max_label_width = 0.0f;///< Measured max label text width for this axis
    glm::vec4 color;             ///< Color for labels and ticks (from persistent state)
    float v_min = 0.0f;
    float v_max = 1.0f;
};

struct layout_result_t {
    // ... existing fields ...
    std::vector<axis_layout_t> v_axes;  ///< All Y-axes (ordered by axis_id)
    bool multi_axis_mode = false;
    float total_v_bar_width = 0.0f;     ///< Sum of all axis bar widths
};
```

**Data Flow:**
```
1. series_data_t::axis_id           ← User-configured via set_series_axis()
2. axis_persistent_state_t          ← Persistent in plot_renderer (color, animation)
3. axis_frame_state_t               ← Computed per-frame (target ranges, series grouping)
4. axis_layout_t                    ← Computed by layout_calculator
                                       (labels, per-axis bar width from measured labels)
5. Consumed by:
   - chrome_renderer (tick marks, conditional grid)
   - text_renderer (colored axis labels)
   - series_renderer (per-series v_min/v_max uniforms)
```

### 4.2 Files to Modify

| File | Changes |
|------|---------|
| `include/vnm_plot/core/types.h` | Add `axis_id` to `series_data_t` |
| `include/vnm_plot/core/layout_calculator.h` | Add `axis_layout_t`, multi-axis layout computation |
| `src/core/layout_calculator.cpp` | Implement per-axis label calculation |
| `src/core/chrome_renderer.cpp` | Conditional grid rendering, per-axis tick marks |
| `src/core/text_renderer.cpp` | Render multiple colored axis label sets |
| `src/core/series_renderer.cpp` | Per-series v_min/v_max uniforms |
| `src/qt/plot_renderer.cpp` | Per-axis range computation, axis state management |
| `include/vnm_plot/qt/plot_widget.h` | Add `set_series_axis()` API |
| `src/qt/plot_widget.cpp` | Implement `set_series_axis()` |

### 4.3 Rendering Pipeline Changes

**Current Flow:**
```
1. Compute global v_range (shared by all series)
2. Calculate layout (single Y-axis labels)
3. Render grid (horizontal + vertical)
4. Render all series (same v_min/v_max uniforms)
5. Render single Y-axis labels
```

**Proposed Flow:**
```
1. Group series by axis_id
2. For each axis group:
   a. Compute v_range from member series
   b. Calculate axis labels
3. Render grid:
   - Vertical (time) grid: always
   - Horizontal grid: only if all series on axis_id=0 (single-axis mode)
4. Render series:
   - Look up v_min/v_max from series' axis
   - Pass per-series uniforms to shader
5. Render Y-axis bars:
   - For each axis: labels + tick marks (colored)
   - Bars arranged side-by-side
```

---

## 5. User Interface / API

### 5.1 Minimal API (Plot_widget)

**Single new function:**

```cpp
/// Assign a series to an axis group.
/// @param series_id  The series to configure
/// @param axis_id    0 = default axis, >0 = independent axis group
///
/// Series with the same axis_id share a Y-axis.
/// The axis color is taken from the first series assigned to it.
///
/// Examples:
///   set_series_axis(1, 0);  // Series 1 on default axis
///   set_series_axis(2, 1);  // Series 2 on independent axis 1
///   set_series_axis(3, 1);  // Series 3 shares axis 1 with series 2
///   set_series_axis(4, 2);  // Series 4 on independent axis 2
void set_series_axis(int series_id, int axis_id);
```

**That's it.** One function, no enums, no modes, no bloat.

### 5.2 Behavior Rules

| Condition | Behavior |
|-----------|----------|
| All **visible** series have `axis_id=0` | Single-axis mode (current behavior, full grid) |
| Any **visible** series has `axis_id>0` | Multi-axis mode (tick marks only, no H-grid) |
| Multiple series same `axis_id` | Share axis, range = union of visible series' ranges |
| Axis color | Assigned on first `set_series_axis()` call, **stable** thereafter |
| Axis becomes empty (all series hidden/removed) | Axis not rendered, but color preserved in cache |
| Axis reappears (series re-enabled) | Uses cached color |
| Invalid series_id | Silently ignored |
| Negative axis_id | Treated as 0 (default axis) |

**Visibility rule:** A series is "visible" if `series.enabled == true` and `series.data_source != nullptr`. Only visible series contribute to axis ranges and multi-axis mode detection.

### 5.3 Optional Future Extensions (not in v1)

If needed later, these could be added without breaking the minimal API:
- `set_axis_color(int axis_id, QColor color)` - override axis color
- `set_axis_label(int axis_id, QString label)` - axis name/unit label
- `set_axis_range(int axis_id, float v_min, float v_max)` - manual range

---

## 6. Visual Layout

### 6.1 Single Y-Axis Mode (axis_id=0 only)

```
+------------------------------------------+--------+
|                                          | 100.0 -|
|           PLOT AREA                      |  75.0 -|
|                                          |  50.0 -|
|           [horizontal grid lines]        |  25.0 -|
|                                          |   0.0 -|
+------------------------------------------+--------+
|              TIME AXIS LABELS                     |
+---------------------------------------------------+
```
Current behavior, unchanged.

### 6.2 Multi-Axis Mode (any visible series has axis_id > 0)

**Two axes example:**
```
+------------------------------------------+--------+--------+
|                                          | 100.0 -| 10000 -|  <- tick marks
|           PLOT AREA                      |  75.0 -|  7500 -|
|                                          |  50.0 -|  5000 -|
|       [no horizontal grid lines]         |  25.0 -|  2500 -|
|                                          |   0.0 -|     0 -|
+------------------------------------------+--------+--------+
|              TIME AXIS LABELS            | (blue) | (red)  |
+------------------------------------------+--------+--------+
```

**Three axes example:**
```
+----------------------------------------+--------+--------+--------+
|                                        |  10.0 -| 1000  -| 0.010 -|
|           PLOT AREA                    |   7.5 -|  750  -| 0.005 -|
|                                        |   5.0 -|  500  -| 0.000 -|
|                                        |   2.5 -|  250  -|-0.005 -|
|                                        |   0.0 -|    0  -|-0.010 -|
+----------------------------------------+--------+--------+--------+
```

**Key visual elements:**
- Each axis bar width computed from its measured labels (V-bar = sum of axis widths)
- Labels colored to match their axis (stable, cached on axis creation)
- Small tick marks (`-`) at label positions (no full horizontal grid)
- Axes ordered by axis_id (lower IDs on left)

---

## 7. Implementation Phases

### Phase 1: Data Structures & API
1. Add `axis_id` field to `series_data_t` in `types.h`
2. Add `axis_persistent_state_t` and `axis_frame_state_t` in `plot_renderer.cpp`
3. Add `axis_layout_t` struct to `layout_calculator.h`
4. Add `set_series_axis()` to `plot_widget.h/.cpp`

### Phase 2: Axis State Management
1. In `plot_renderer.cpp`: build axis groups from contributing series (using `series_contributes()`)
2. Compute per-axis v_range (union of contributing member series ranges)
3. Determine multi-axis mode flag (any contributing series has axis_id > 0)
4. Store axis states for use by renderers

### Phase 3: Layout Calculation
1. Extend `Layout_calculator` to compute labels for each axis
2. Measure per-axis label widths, compute per-axis bar width
3. Calculate x_offset for each axis bar (side-by-side positioning)
4. Return `std::vector<axis_layout_t>` and `total_v_bar_width` in layout result

### Phase 4: Rendering Updates
1. **Chrome renderer:**
   - Skip horizontal grid in multi-axis mode
   - Render tick marks per axis (small horizontal lines in axis bar)
2. **Text renderer:**
   - Render labels for each axis with correct color and x_offset
3. **Series renderer:**
   - Look up v_min/v_max from series' axis state
   - Pass correct uniforms per series

### Phase 5: V-Bar Width & Polish
1. Use `layout_result.total_v_bar_width` (sum of per-axis measured widths)
2. Animate V-bar width changes smoothly
3. Handle edge cases (no series, series removed, axis becomes empty)

---

## 8. Technical Details

### 8.1 Axis Ordering
Axes are ordered by axis_id in the V-bar:
- Only axes with assigned series are rendered
- axis_id=0 (default) appears first (leftmost) if it has series
- axis_id=1, 2, 3... follow in ascending order
- Empty axes are skipped (no gaps) - this includes axis_id=0 if no series use it

### 8.2 Range Computation Per Axis
```cpp
for each axis_id:
    v_min = +INF, v_max = -INF
    for each series with this axis_id:
        [series_min, series_max] = compute_series_range(series)
        v_min = min(v_min, series_min)
        v_max = max(v_max, series_max)
    apply padding (5%)
    animate toward target range
```

### 8.3 Tick Mark Rendering
In multi-axis mode, for each axis:
```cpp
for each label in axis.labels:
    draw_line(
        x1 = axis.x_offset + axis.bar_width - tick_length,
        x2 = axis.x_offset + axis.bar_width,
        y  = label.y,
        color = axis.color
    )
```

### 8.4 Series Rendering Uniforms
Currently all series share uniforms. Change to:
```cpp
for each series:
    axis = get_axis_for_series(series.id)
    glUniform1f(u_v_min, axis.v_min)
    glUniform1f(u_v_max, axis.v_max)
    render_series(series)
```

### 8.5 Edge Cases

| Condition | Behavior |
|-----------|----------|
| `set_series_axis(invalid_id, n)` | Silently ignored (series doesn't exist) |
| `set_series_axis(id, negative)` | Treated as axis_id=0 (default axis) |
| Series removed/reassigned → axis empty | Axis hidden, but color preserved in cache |
| Axis reappears (series re-enabled/reassigned) | Uses cached color, V-bar expands |
| Series with no data points | Excluded from range computation |
| All visible series on axis have no data | Axis uses default range [0.0, 1.0] |
| Hidden/disabled series | Excluded from range computation and multi-axis detection |
| axis_id=0 with no visible series | Skipped (not rendered), same as any empty axis |
| Toggle series visibility | Axis color remains stable (does not change) |
| Wide labels (large numbers) | Per-axis bar width expands to fit, no clipping |

**Axis color stability:** The axis color is assigned when `set_series_axis()` is first called for that axis_id, using the color of the series being assigned. The color is cached in persistent state and remains stable regardless of:
- Series visibility toggles
- Series removal/re-addition
- Order of subsequent series additions

To change an axis color, use `set_axis_color()` (future extension). The cache is never automatically invalidated.

### 8.6 LOD Interaction

The existing LOD (Level-of-Detail) system is unaffected by multi-axis:
- LOD selection is based on X-axis (time) zoom level, not Y-range
- Y-range is computed *from* the already-selected LOD data
- Per-axis ranges simply means computing multiple ranges instead of one global range
- No changes to LOD cache structure or keying required

---

## 9. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Performance with many axes | Slower range computation | Cache per-axis ranges, throttle updates |
| Visual clutter with 4+ axes | Hard to read | User's choice - no artificial limit |
| Per-series uniform binding | More draw calls | Already doing per-series loops |
| LOD cache per axis | Memory increase | Reuse existing LOD infrastructure |

---

## 10. Success Criteria

- [ ] Two signals with vastly different scales are both fully visible
- [ ] Each axis has colored labels matching its signal(s)
- [ ] Tick marks appear on each axis bar (no confusing horizontal grid)
- [ ] V-bar width expands/contracts smoothly with axis count
- [ ] Backward compatible: all series on axis_id=0 behaves exactly as current single-axis mode
- [ ] Axis grouping works: multiple series on same axis_id share range
- [ ] Edge cases handled: empty axes skipped, hidden series excluded, no-data defaults to [0,1]

---

## 11. Additional Design Decisions

| Decision | Choice |
|----------|--------|
| Axis ordering | No gaps - axes ordered by axis_id ascending (skip empty axes) |
| Mixed mode (axis_id=0 + others) | All axes equal - default axis also uses tick marks, no full grid |
| Terminology | "Default axis" (not "shared") - axis_id=0 behaves like any other axis in multi-axis mode |

---

## 12. Next Steps

1. ~~Review this spec and confirm decisions~~ **DONE**
2. Begin Phase 1 implementation

---

## 13. Detailed Implementation Guide

This section provides specific code changes required, with file paths and line references.

---

### 13.1 Phase 1: Data Structures & API

#### 13.1.1 Add `axis_id` to `series_data_t`

**File:** `include/vnm_plot/core/types.h`
**Location:** Inside `struct series_data_t` (lines 319-359)

**Current structure:**
```cpp
struct series_data_t
{
    int id = 0;
    bool enabled = true;
    Display_style style = Display_style::LINE;
    glm::vec4 color = glm::vec4(0.16f, 0.45f, 0.64f, 1.0f);

    std::shared_ptr<Data_source> data_source;
    Data_access_policy access;
    // ...
};
```

**Add after `colormap_config_t colormap;` (line 329):**
```cpp
    int axis_id = 0;  ///< Y-axis assignment: 0=default, >0=independent axis group
```

**Rationale:** Minimal change to existing struct. Default value 0 ensures backward compatibility.

---

#### 13.1.2 Add axis state structs

**File:** `src/qt/plot_renderer.cpp`
**Location:** Inside `impl_t` struct

**Add persistent state (survives across frames):**
```cpp
/// Persistent state per axis - for animation and color stability.
struct axis_persistent_state_t {
    glm::vec4 color;             ///< Assigned on first creation, stable
    float anim_v_min = 0.0f;     ///< Current animated v_min
    float anim_v_max = 1.0f;     ///< Current animated v_max
    std::chrono::steady_clock::time_point last_update;
};
std::unordered_map<int, axis_persistent_state_t> axis_persistent;
```

**Add per-frame state (rebuilt each frame):**
```cpp
/// Per-frame computed state from contributing series.
struct axis_frame_state_t {
    int axis_id = 0;
    float target_v_min = 0.0f;   ///< Target range from data
    float target_v_max = 1.0f;
    std::vector<int> series_ids; ///< Contributing series
};
```

**Rationale:** Splitting persistent vs per-frame state ensures:
- Animation works correctly (anim values persist)
- Color is stable (assigned once, cached)
- Target ranges update each frame from visible data

---

#### 13.1.3 Add `axis_layout_t` to layout result

**File:** `include/vnm_plot/core/layout_calculator.h`
**Location:** Before `Layout_calculator` class (after line 17)

**Add new struct:**
```cpp
/// Layout result for a single Y-axis bar.
struct axis_layout_t
{
    int axis_id = 0;
    std::vector<v_label_t> labels;
    float x_offset = 0.0f;        ///< Horizontal offset from plot area right edge
    float bar_width = 0.0f;       ///< Computed from max_label_width + padding
    float max_label_width = 0.0f; ///< Measured max label text width
    glm::vec4 color;              ///< Color for labels and tick marks
    float v_min = 0.0f;
    float v_max = 1.0f;
};
```

**File:** `include/vnm_plot/core/types.h`
**Location:** Inside `struct frame_layout_result_t` (lines 392-411)

**Add after `std::vector<v_label_t> v_labels;` (line 401):**
```cpp
    std::vector<axis_layout_t> v_axes;  ///< Multi-axis mode: per-axis layouts
    bool multi_axis_mode = false;       ///< True if any visible series has axis_id > 0
    float total_v_bar_width = 0.0f;     ///< Sum of all axis bar widths
```

**Note:** We keep `v_labels` for backward compatibility in single-axis mode. In multi-axis mode, renderers use `v_axes` instead.

---

#### 13.1.4 Add `set_series_axis()` API

**File:** `include/vnm_plot/qt/plot_widget.h`
**Location:** After `void remove_series(int id);` (line 60)

**Add:**
```cpp
    /// Assign a series to a Y-axis group.
    /// @param series_id  The series to configure (must exist)
    /// @param axis_id    0 = default axis, >0 = independent axis group
    /// Series with the same axis_id share a Y-axis and scale.
    void set_series_axis(int series_id, int axis_id);
```

**File:** `src/qt/plot_widget.cpp`
**Location:** After `remove_series()` (after line 103)

**Add implementation:**
```cpp
void Plot_widget::set_series_axis(int series_id, int axis_id)
{
    // Treat negative axis_id as 0 (default)
    if (axis_id < 0) {
        axis_id = 0;
    }

    std::unique_lock lock(m_series_mutex);
    auto it = m_series.find(series_id);
    if (it == m_series.end() || !it->second) {
        return;  // Silently ignore invalid series_id
    }

    // Cache axis color on FIRST assignment to this axis_id
    if (m_axis_colors.find(axis_id) == m_axis_colors.end()) {
        m_axis_colors[axis_id] = it->second->color;
    }

    it->second->axis_id = axis_id;
    update();
}
```

**Add to Plot_widget member variables:**
```cpp
std::unordered_map<int, glm::vec4> m_axis_colors;  ///< Cached axis colors (set on first assignment)
```

**Renderer accesses cached colors:**
```cpp
// In plot_renderer, when building axis layout:
glm::vec4 get_axis_color(int axis_id) {
    auto colors = m_widget->get_axis_colors();  // Thread-safe getter
    auto it = colors.find(axis_id);
    if (it != colors.end()) {
        return it->second;
    }
    return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);  // Fallback
}
```

**Rationale:** Color is assigned on first `set_series_axis()` call and remains stable. The cache lives in the widget (shared state) and is never automatically invalidated. This ensures color is based on assignment order, not visibility.

---

### 13.2 Phase 2: Axis State Management

#### 13.2.1 Build axis groups from series map

**File:** `src/qt/plot_renderer.cpp`
**Location:** Inside `impl_t` struct (around line 557)

**Add visibility predicate (used everywhere):**
```cpp
/// Returns true if series contributes to rendering and range computation.
static bool series_contributes(const series_data_t& s)
{
    return s.enabled && s.data_source != nullptr;
}
```

**Add helper function:**
```cpp
/// Build per-frame axis states from contributing series.
std::map<int, axis_frame_state_t> build_axis_frame_states(
    const std::map<int, std::shared_ptr<series_data_t>>& series_map)
{
    std::map<int, axis_frame_state_t> axes;

    for (const auto& [id, series] : series_map) {
        if (!series || !series_contributes(*series)) {
            continue;
        }

        int axis_id = series->axis_id;
        if (axis_id < 0) axis_id = 0;

        auto& axis = axes[axis_id];
        axis.axis_id = axis_id;
        axis.series_ids.push_back(id);
        // Note: axis color is NOT set here - it comes from widget's m_axis_colors cache
    }

    return axes;
}

/// Check if we're in multi-axis mode (any contributing series has axis_id > 0).
bool is_multi_axis_mode(const std::map<int, axis_frame_state_t>& axes)
{
    return std::any_of(axes.begin(), axes.end(),
        [](const auto& kv) { return kv.first > 0; });
}
```

**Key point:** Axis color is fetched from the widget's `m_axis_colors` cache (set on first `set_series_axis()` call), not determined per-frame from visible series.

---

#### 13.2.2 Compute per-axis v_range

**File:** `src/qt/plot_renderer.cpp`
**Location:** Modify `compute_global_v_range()` (lines 353-433)

**Create new function alongside existing one:**
```cpp
/// Compute v_range for a single axis (subset of series).
std::pair<float, float> compute_axis_v_range(
    const std::map<int, std::shared_ptr<series_data_t>>& series_map,
    const std::vector<int>& series_ids,
    std::unordered_map<int, series_minmax_cache_t>& cache_map,
    bool use_lod_cache,
    float fallback_min,
    float fallback_max)
{
    float v_min = std::numeric_limits<float>::max();
    float v_max = std::numeric_limits<float>::lowest();
    bool have_any = false;

    for (int id : series_ids) {
        auto it = series_map.find(id);
        if (it == series_map.end()) continue;

        const auto& series = it->second;
        if (!series || !series->enabled || !series->data_source) {
            continue;
        }
        // ... same logic as compute_global_v_range but for subset ...
    }

    if (!have_any) {
        return {fallback_min, fallback_max};
    }
    return {v_min, v_max};
}
```

**Rationale:** Reuses existing min/max logic but operates on a subset of series (those belonging to one axis).

---

### 13.3 Phase 3: Layout Calculation

#### 13.3.1 Extend Layout_calculator for multi-axis

**File:** `include/vnm_plot/core/layout_calculator.h`
**Location:** Modify `parameters_t` struct (lines 29-66)

**Add new fields:**
```cpp
    // Multi-axis support
    struct axis_input_t {
        int axis_id;
        float v_min, v_max;
        glm::vec4 color;  // From persistent cache
    };
    std::vector<axis_input_t> axes;  ///< Axis states (empty = single-axis mode)
```

**File:** `src/core/layout_calculator.cpp`
**Location:** In `calculate()` method (lines 411-1086)

The existing v_labels calculation (lines 420-681) needs to be wrapped in a loop over axes. The key insight is that the current code already computes labels for a given `v_min`/`v_max` range - we just need to call it multiple times with different ranges.

**Modification approach:**
1. If `params.axes.empty()`, use current single-axis logic (backward compatible)
2. Otherwise, loop over each axis in `params.axes`:
   - Compute labels for this axis (reuse existing logic)
   - **Measure max label width** for this axis
   - **Compute bar_width** from measured width + padding
   - Compute `x_offset` = sum of previous axis bar widths
   - Append to `res.v_axes`
3. Sum all bar widths into `res.total_v_bar_width`

**Pseudocode for multi-axis section:**
```cpp
if (!params.axes.empty()) {
    res.multi_axis_mode = true;
    float x_offset = 0.0f;
    res.total_v_bar_width = 0.0f;

    for (const auto& axis : params.axes) {
        axis_layout_t axis_layout;
        axis_layout.axis_id = axis.axis_id;
        axis_layout.color = axis.color;
        axis_layout.v_min = axis.v_min;
        axis_layout.v_max = axis.v_max;

        // Compute labels for this axis (reuse existing v_label logic)
        // This populates axis_layout.labels and measures each label
        compute_axis_labels(params, axis.v_min, axis.v_max, axis_layout);

        // Per-axis bar width from measured labels (NO CLIPPING)
        axis_layout.bar_width = std::max(
            k_min_bar_width_px,
            axis_layout.max_label_width + k_v_label_horizontal_padding_px + k_tick_length_px);

        axis_layout.x_offset = x_offset;
        x_offset += axis_layout.bar_width;
        res.total_v_bar_width += axis_layout.bar_width;

        res.v_axes.push_back(std::move(axis_layout));
    }
}
```

**Key change from earlier design:** Each axis bar width is computed from its own measured labels, not a fixed "standard" width. This prevents label clipping.

---

### 13.4 Phase 4: Rendering Updates

#### 13.4.1 Chrome Renderer: Conditional grid

**File:** `src/core/chrome_renderer.cpp`
**Location:** In `render_grid_and_backgrounds()` (lines 128-262)

**Current code (lines 178-186):**
```cpp
const grid_layer_params_t vertical_levels = calculate_grid_params(
    double(ctx.v0), double(ctx.v1), pl.usable_height, ctx.adjusted_font_px);
const grid_layer_params_t horizontal_levels = build_time_grid(...);
// ...
prims.draw_grid_shader(main_origin, main_size, grid_rgb, vertical_levels_gl, horizontal_levels);
```

**Modify to:**
```cpp
const grid_layer_params_t horizontal_levels = build_time_grid(...);

grid_layer_params_t vertical_levels;
if (!pl.multi_axis_mode) {
    // Single-axis mode: full horizontal grid
    vertical_levels = calculate_grid_params(
        double(ctx.v0), double(ctx.v1), pl.usable_height, ctx.adjusted_font_px);
}
// else: vertical_levels stays empty (no horizontal grid lines)

const grid_layer_params_t vertical_levels_gl = flip_grid_levels_y(vertical_levels, main_size.y);
prims.draw_grid_shader(main_origin, main_size, grid_rgb, vertical_levels_gl, horizontal_levels);
```

**Rationale:** In multi-axis mode, pass empty `vertical_levels` to skip horizontal grid.

---

#### 13.4.2 Chrome Renderer: Per-axis tick marks

**File:** `src/core/chrome_renderer.cpp`
**Location:** After grid rendering (around line 248)

**Current code renders single v-bar ticks (lines 249-254):**
```cpp
if (!skip_gl && pl.v_bar_width > 0.5 && vertical_tick_levels_gl.count > 0) {
    const glm::vec2 top_left{float(pl.usable_width), 0.0f};
    const glm::vec2 size{float(pl.v_bar_width), float(pl.usable_height)};
    // ...
}
```

**Modify for multi-axis:**
```cpp
if (pl.multi_axis_mode) {
    // Multi-axis: render tick marks for each axis
    for (const auto& axis : pl.v_axes) {
        grid_layer_params_t tick_levels = build_vertical_tick_levels(
            axis.labels, /* empty main_levels for standalone ticks */);
        grid_layer_params_t tick_levels_gl = flip_grid_levels_y(tick_levels, main_size.y);

        if (!skip_gl && axis.bar_width > 0.5 && tick_levels_gl.count > 0) {
            const glm::vec2 top_left{float(pl.usable_width + axis.x_offset), 0.0f};
            const glm::vec2 size{float(axis.bar_width), float(pl.usable_height)};
            const glm::vec2 origin = to_gl_origin(ctx, top_left, size);
            // Use axis.color for ticks
            prims.draw_grid_shader(origin, size, axis.color, tick_levels_gl, empty_levels);
        }
    }
} else {
    // Single-axis: existing code
    // ...
}
```

---

#### 13.4.3 Text Renderer: Colored multi-axis labels

**File:** `src/core/text_renderer.cpp`
**Location:** In `render_axis_labels()` (lines 137-229)

**Current code uses single font color (line 141):**
```cpp
const glm::vec4 font_color = dark_mode ? glm::vec4(1,1,1,1) : glm::vec4(0,0,0,1);
```

**Modify for multi-axis:**
```cpp
if (pl.multi_axis_mode) {
    // Multi-axis: render labels for each axis with its color
    for (const auto& axis : pl.v_axes) {
        const float right_edge_x = static_cast<float>(
            pl.usable_width + axis.x_offset + axis.bar_width - k_v_label_horizontal_padding_px);
        const float min_x = static_cast<float>(
            pl.usable_width + axis.x_offset + k_text_margin_px);

        // Scissor to this axis bar
        glScissor(
            static_cast<GLint>(lround(pl.usable_width + axis.x_offset)), scissor_y,
            static_cast<GLsizei>(lround(axis.bar_width)), ...);

        // Use axis.color for labels
        for (const auto& label : axis.labels) {
            // ... draw_label logic with axis.color ...
        }
        m_fonts->draw_and_flush(ctx.pmv, axis.color);
    }
} else {
    // Single-axis: existing code with standard font_color
    // ...
}
```

**Key changes:**
- Loop over `pl.v_axes` instead of `pl.v_labels`
- Adjust `right_edge_x` and `min_x` using axis's `x_offset`
- Scissor to each axis bar's region
- Use `axis.color` instead of theme `font_color`

---

#### 13.4.4 Series Renderer: Per-series uniforms

**File:** `src/core/series_renderer.cpp`
**Location:** In `set_common_uniforms()` (lines 552-577)

**Current code (lines 566-567):**
```cpp
glUniform1f(program.uniform_location("v_min"), ctx.v0);
glUniform1f(program.uniform_location("v_max"), ctx.v1);
```

**The issue:** Currently `ctx.v0`/`ctx.v1` is the global range. For multi-axis, each series needs its own axis's range.

**Solution:** Pass axis ranges via render context or look up per-series.

**Option A: Add axis states to frame_context_t**

**File:** `include/vnm_plot/core/types.h`
**Location:** In `frame_context_t` (lines 499-527)

**Add minimal render state struct and map:**
```cpp
/// Minimal axis state for rendering (v_min/v_max only).
struct axis_render_state_t {
    float v_min = 0.0f;
    float v_max = 1.0f;
};

// In frame_context_t:
std::map<int, axis_render_state_t> axis_render_states;  ///< axis_id -> render state
```

**Then in series_renderer.cpp render loop (line 692):**
```cpp
for (const auto& [id, s] : series) {
    // ...
    float series_v_min = ctx.v0;  // default
    float series_v_max = ctx.v1;

    if (!ctx.axis_render_states.empty()) {
        int axis_id = s->axis_id;
        if (axis_id < 0) axis_id = 0;
        auto it = ctx.axis_render_states.find(axis_id);
        if (it != ctx.axis_render_states.end()) {
            series_v_min = it->second.v_min;
            series_v_max = it->second.v_max;
        }
    }

    // Later, when setting uniforms:
    glUniform1f(program.uniform_location("v_min"), series_v_min);
    glUniform1f(program.uniform_location("v_max"), series_v_max);
}
```

**Rationale:** Uses a minimal struct containing only v_min/v_max for rendering. The full persistent/frame state lives in plot_renderer; only the final animated values are passed to series_renderer via frame_context_t.

---

### 13.5 Phase 5: V-Bar Width & Polish

#### 13.5.1 Calculate total V-bar width

**File:** `src/qt/plot_renderer.cpp`
**Location:** In frame setup where `vbar_width_pixels` is determined

**Current:** Uses `m_data_cfg.vbar_width` or computed from labels.

**New approach:** Total V-bar width is the sum of per-axis bar widths (from layout_result).

```cpp
// In multi-axis mode, use sum from layout calculation
double effective_vbar_width = layout_result.multi_axis_mode
    ? layout_result.total_v_bar_width
    : measured_single_axis_width;  // existing logic
```

**Key change:** No fixed multiplication. Each axis width is computed from its measured labels, then summed. This prevents clipping of wide labels (large numbers, scientific notation).

---

#### 13.5.2 Animate V-bar width changes

**File:** `src/qt/plot_widget.cpp`
**Location:** Existing animation logic (lines 180-183)

The widget already has V-bar width animation:
```cpp
QBasicTimer m_vbar_width_timer;
QElapsedTimer m_vbar_width_anim_elapsed;
double m_vbar_width_anim_start_px = 0.0;
double m_vbar_width_anim_target_px = 0.0;
```

**This infrastructure can be reused.** When total V-bar width changes:
1. Compute new total width from `layout_result.total_v_bar_width`
2. Set as new animation target
3. Existing animation code handles smooth transition

---

### 13.6 Key Code References Summary

| Component | File | Key Lines | Change |
|-----------|------|-----------|--------|
| `series_data_t::axis_id` | `types.h` | 319-359 | Add field |
| `m_axis_colors` | `plot_widget.h/cpp` | member | Axis color cache (set on first assignment) |
| `axis_persistent_state_t` | `plot_renderer.cpp` | impl_t | Animation state (anim_v_min/max) |
| `axis_frame_state_t` | `plot_renderer.cpp` | impl_t | Per-frame targets, series grouping |
| `axis_render_state_t` | `types.h` | frame_context_t | Minimal v_min/v_max for series renderer |
| `axis_layout_t` | `layout_calculator.h` | before class | Labels, per-axis width, color |
| `frame_layout_result_t` | `types.h` | 392-411 | Add v_axes, multi_axis_mode, total_v_bar_width |
| `set_series_axis()` | `plot_widget.h/cpp` | 60, 103 | New API, caches color on first call |
| `series_contributes()` | `plot_renderer.cpp` | impl_t | Visibility predicate |
| Axis state building | `plot_renderer.cpp` | 557+ | `build_axis_frame_states()` |
| Per-axis range | `plot_renderer.cpp` | 353-433 | Modify for per-axis |
| Layout multi-axis | `layout_calculator.cpp` | 411-681 | Loop over axes, per-axis bar width |
| Grid conditional | `chrome_renderer.cpp` | 178-186 | Skip H-grid in multi-axis |
| Per-axis ticks | `chrome_renderer.cpp` | 249-254 | Loop over axes |
| Colored labels | `text_renderer.cpp` | 137-229 | Per-axis color from cache |
| Series uniforms | `series_renderer.cpp` | 566-567 | Per-series v_min/v_max |
| V-bar total width | `plot_renderer.cpp` | varies | Sum of per-axis measured widths |

---

### 13.7 Testing Strategy

#### Unit Tests
1. `axis_persistent_state_t` color caching on first axis creation
2. `axis_frame_state_t` population from contributing series only
3. Range computation for axis with multiple series
4. Empty axis handling (no series, all hidden)
5. `series_contributes()` predicate correctness

#### Integration Tests
1. Add two series with different axis_ids, verify both scales visible
2. Group two series on same axis_id, verify shared range
3. Remove series, verify axis disappears (color preserved in cache)
4. Toggle series.enabled, verify range updates but axis color stable

#### Mode Transition Tests
1. Toggle visibility of axis_id>0 series, verify grid/V-bar behavior matches "visibility drives mode"
2. All axis_id>0 series hidden → single-axis mode (full grid)
3. Re-enable axis_id>0 series → multi-axis mode (tick marks only)

#### Color Stability Tests
1. Toggle enabled/visible on first series, axis color must NOT change
2. Disable all series on an axis, re-enable → same cached color
3. Series added in different order → axis color from first assignment

#### Label Width Tests
1. Large magnitude labels (e.g., 1,234,567) must fit without clipping
2. Scientific notation labels must fit without clipping
3. Per-axis bar widths differ when label widths differ
4. Total V-bar width = sum of individual axis bar widths

#### Visual Tests
1. Tick marks appear on each axis bar
2. Label colors match cached axis colors (not current first series)
3. No horizontal grid in multi-axis mode
4. V-bar width animates smoothly when axis count or label widths change
5. Single-axis mode unchanged (backward compatibility)

