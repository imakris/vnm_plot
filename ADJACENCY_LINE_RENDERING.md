# Adjacency-Aware Line Rendering for COLORMAP_LINE

## Objective

Implement adjacency-aware line rendering to fix visual quality issues where line joins and corners differ between LODs (Levels of Detail) and appear poor.

### Problem Statement

The original COLORMAP_LINE rendering used per-segment quads with no adjacency information:
- Each line segment was rendered independently
- No information about neighboring segments was available
- Line joins/corners looked inconsistent across different zoom levels (LODs)
- Visual artifacts appeared at segment boundaries

### Solution Approach

Use OpenGL's adjacency primitives to provide geometric context for each line segment, enabling proper join rendering.

## Implementation Strategy

### Phase 1: Infrastructure Setup (COMPLETED)

**Goal**: Establish the foundation for adjacency-aware rendering without changing visual output.

**Changes Made**:

1. **Drawing Mode Update** (`src/core/series_renderer.cpp`)
   - Changed from `GL_LINE_STRIP` to `GL_LINE_STRIP_ADJACENCY`
   - Updated minimum vertex count check (4 vertices required for adjacency)

2. **Index Buffer Generation** (`src/core/series_renderer.cpp`)
   - Implemented indexed drawing using `glDrawElements`
   - Duplicate first and last vertices to provide boundary adjacency:
     ```
     Original vertices: [v0, v1, v2, v3, v4]
     Adjacency indices: [v0, v0, v1, v2, v3, v4, v4]
                         ^adj ^actual vertices...    ^adj
     ```

3. **Adjacency-Aware Geometry Shader** (`shaders/plot_line_adjacency.geom`)
   - Input: `layout(lines_adjacency)` - receives 4 vertices per segment
   - Vertex order:
     - `gs_in[0]`: Previous vertex (for incoming join)
     - `gs_in[1]`: Current segment start
     - `gs_in[2]`: Current segment end
     - `gs_in[3]`: Next vertex (for outgoing join)

4. **Shader Registration**
   - Added shader to Qt resource system (`vnm_plot.qrc`)
   - Configured COLORMAP_LINE to use adjacency shader (`function_plotter.cpp`)

**Status**: Infrastructure complete, builds successfully, provides adjacency data to shader.

### Phase 2: Join Geometry Generation (NEXT STEP)

**Goal**: Use adjacency information to generate proper geometric joins.

**Approach**: Generate trapezoids or triangles at segment joins to maintain consistent edge appearance.

#### Option A: Mitered Joins
- Calculate angle between adjacent segments
- Extend segment endpoints to create sharp mitered corners
- Best for: Smooth, continuous data
- Challenge: Need to handle acute angles (miter limit)

```
    Previous segment         Next segment
         /                        \
        /                          \
       /______(miter point)_________\
            Current segment
```

#### Option B: Bevel Joins
- Fill the gap between segments with a triangle
- More robust, no special cases for acute angles
- Best for: General-purpose rendering

```
    Previous segment         Next segment
         /                        \
        /        /-------\          \
       /_______/         \_________\
       Current segment
```

#### Option C: Round Joins
- Generate small arc between segments
- Smoothest appearance
- Most expensive (requires more vertices)

**Recommended**: Start with bevel joins (Option B) for robustness.

#### Implementation Plan for Phase 2:

1. **Calculate Join Geometry**:
   ```glsl
   // In geometry shader
   vec2 dir_prev = normalize(v1 - v0);  // Direction from previous
   vec2 dir_curr = normalize(v2 - v1);  // Direction of current segment
   vec2 dir_next = normalize(v3 - v2);  // Direction to next
   ```

2. **Emit Trapezoid/Triangle for Each Join**:
   - At segment start: use `dir_prev` and `dir_curr`
   - At segment end: use `dir_curr` and `dir_next`

3. **Update Output**:
   - Change from `line_strip` to `triangle_strip`
   - Increase `max_vertices` (e.g., 6-8 vertices per segment)
   - Emit quad/trapezoid covering the line width

4. **Handle Edge Cases**:
   - First segment: `v0 == v1` (no previous, use current direction)
   - Last segment: `v2 == v3` (no next, use current direction)
   - Degenerate segments (zero length)

### Phase 3: Width-Aware Rendering (FUTURE)

**Goal**: Make joins properly respect line width.

- Add line width uniform to shader
- Calculate perpendicular offsets based on width
- Generate proper thick line geometry with correct joins

### Phase 4: Colormap Integration (FUTURE)

**Goal**: Apply colormap along the line based on signal values.

- Pass signal value per vertex
- Sample colormap texture in fragment shader
- Interpolate colors across join geometry

## Technical Notes

### Why Adjacency Primitives?

OpenGL's `GL_LINE_STRIP_ADJACENCY` provides exactly 4 vertices to the geometry shader:
- Minimal data overhead (just boundary vertex duplication)
- Direct access to neighbor information
- Efficient for GPU processing

### Performance Considerations

**Current Impact**: Minimal
- Index buffer generation is CPU-side but simple
- `glDrawElements` vs `glDrawArrays` negligible difference
- Shader complexity unchanged (Phase 1)

**Future Impact** (Phase 2+):
- More vertices emitted per segment (triangles vs lines)
- Increased fragment shader invocations
- Trade-off: Better visual quality vs slightly higher GPU load
- Should still be well within budget for typical use cases

### Alternative Approaches Considered

1. **CPU-side join generation**: Too slow, would require re-upload on every LOD change
2. **Compute shader preprocessing**: Overkill, adds complexity
3. **Instanced quads**: Requires more complex vertex layout changes
4. **Geometry shader amplification**: ✅ CHOSEN - Clean, efficient, GPU-native

## Testing Strategy

### Phase 1 Validation:
- ✅ Build succeeds
- ✅ Shader compiles
- Next: Visual verification that rendering still works

### Phase 2 Validation:
- Zoom in/out to test different LODs
- Verify joins look consistent across scales
- Check corner cases (very acute/obtuse angles)
- Performance profiling

## References

- OpenGL Specification: Adjacency Primitives
- Original issue: "line is built as per-segment quads with no adjacency, so joins/corners differ between LODs and look poor"
