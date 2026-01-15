# Multi Y-Axis Spec Review

**Revision 4** — Validation after spec updates

Context: Review of `MULTI_YAXIS_SPEC.md` for inconsistencies, ambiguous definitions, and implementation risks.

**Requirements from user:**
- Multi-axis mode driven by visibility (not total series count)
- Axis colors must be stable across visibility toggles
- Label clipping is not acceptable

---

## Validation Summary

All five originally identified issues have been addressed in the updated spec:

| Issue | Status | Verification |
|-------|--------|--------------|
| 1. Multi-axis mode detection | ✓ FIXED | Lines 202-203, 211, 524, 603-609 |
| 2. Axis color stability | ✓ FIXED | Lines 39, 205-207, 365, 368-375, 564-592 |
| 3. Per-frame vs persistent state | ✓ FIXED | Lines 71-93 (separate structs) |
| 4. Label clipping | ✓ FIXED | Lines 41, 102-103, 366, 742-756 |
| 5. Color persistence on empty axis | ✓ FIXED | Lines 206-207, 359-360 |

---

## Detailed Verification

### 1) Multi-axis mode detection — FIXED ✓

**What was wrong:** Spec said "any series has axis_id>0" but implementation checked "enabled series."

**Now correct:**
- Section 5.2 lines 202-203: "All **visible** series... | Any **visible** series..."
- Line 211: Visibility rule defining "visible" = enabled + has data_source
- Line 524: Implementation comment "True if any visible series has axis_id > 0"
- Lines 603-609: `series_contributes()` predicate used everywhere

**Consistent throughout spec and implementation guide.**

---

### 2) Axis color stability — FIXED ✓

**What was wrong:** Color recalculated each frame from first enabled series, causing instability.

**Now correct:**
- Line 39: "color assigned on axis creation, stable across visibility changes"
- Line 205: "Axis color | Assigned on first `set_series_axis()` call, **stable** thereafter"
- Lines 365, 368-375: Edge case and explanation of color stability
- Lines 564-592: Implementation with `m_axis_colors` cache, color assigned once on first assignment

**Color is now explicitly persistent and decoupled from visibility.**

---

### 3) Per-frame vs persistent state — FIXED ✓

**What was wrong:** `axis_state_t` was "per-frame" but contained animation targets, which is contradictory.

**Now correct:**
- Lines 71-81: `axis_persistent_state_t` for color and animation state (survives across frames)
- Lines 83-93: `axis_frame_state_t` for targets computed each frame

**Clean separation between persistent state and per-frame computed values.**

---

### 4) Label clipping prevention — FIXED ✓

**What was wrong:** Fixed "standard" bar width would clip wide labels.

**Now correct:**
- Line 41: "width computed from label measurements"
- Lines 102-103: `bar_width` and `max_label_width` in axis_layout_t
- Line 366: Edge case "Wide labels | Per-axis bar width expands to fit, no clipping"
- Lines 742-756: Implementation computes bar_width from measured labels with comment "NO CLIPPING"

**Per-axis width based on actual label measurements. Explicit no-clipping guarantee.**

---

### 5) Color persistence when axis empty — FIXED ✓

**What was wrong:** No rule for whether color survives when axis becomes empty.

**Now correct:**
- Lines 206-207: "Axis becomes empty | color preserved in cache" and "Axis reappears | Uses cached color"
- Lines 359-360: Same in edge cases table

**Explicit rule: color is preserved, axis reappears with same color.**

---

## Remaining Observations (Non-blocking)

### Minor: Fallback behavior for label overflow

The spec now prevents clipping by expanding bar width, but doesn't define a maximum total V-bar width or fallback if it would exceed available space. In practice, this is unlikely to be an issue, but could be defined as:
- Maximum total V-bar width = X% of widget width
- If exceeded: reduce tick density or switch to scientific notation

**Assessment:** Not blocking. Implementation can handle this edge case pragmatically.

---

## Cross-Reference Verification

| Location | Claim | Consistent With |
|----------|-------|-----------------|
| 5.2:202-203 | "visible series" | 3:44, 13.2.1:603-609 |
| 5.2:205 | "stable" color | 3:39, 8.5:365, 13.1.4:564-592 |
| 4.1:71-93 | Persistent vs per-frame state | 8.5:368-375, 13.1.4:574-592 |
| 4.1:102-103 | Per-axis bar_width | 13.3.1:742-756 |
| 8.5:206-207 | Color preserved on empty | 8.5:359-360 |

All cross-references verified consistent.

---

## Conclusion

**Status: APPROVED**

The specification now correctly addresses all requirements:
1. ✓ Multi-axis mode driven by visibility
2. ✓ Stable axis colors
3. ✓ No label clipping
4. ✓ Clean separation of persistent vs per-frame state
5. ✓ Color persistence across axis visibility changes

The spec is internally consistent and ready for implementation.
