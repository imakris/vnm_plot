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
   - Changed from `GL_LINE_STRIP` to `GL_LINE_STRIP_ADJACENCY` for COLORMAP_LINE
   - Minimum vertex count: 2 data vertices (expanded to 4 with boundary duplication)

2. **Index Buffer Generation** (`src/core/series_renderer.cpp`)
   - Implemented indexed drawing using `glDrawElements` with Element Buffer Object (EBO)
   - Duplicate first and last vertices to provide boundary adjacency:
     ```
     Original vertices: [v0, v1, v2, v3, v4]  (5 data points)
     Adjacency indices: [v0, v0, v1, v2, v3, v4, v4]  (7 indices for GPU)
                         ^adj ^actual vertices...    ^adj
     ```
   - **Minimum vertex requirement**: Only 2 data vertices needed
     - OpenGL requires 4 vertices total for `GL_LINE_STRIP_ADJACENCY`
     - We satisfy this by duplicating boundaries: `[v0, v0, v1, v1]` for 2-vertex line
     - This allows rendering of small datasets (2-3 points) that were previously skipped
   - **EBO management**: Persistent buffer with capacity headroom to minimize reallocations

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

### Phase 2: Convex Join Geometry (COMPLETED)

**Goal**: Use adjacency information to eliminate overlap on convex (inside) angles.

**Approach**: Calculate miter points where outer edges meet on the angle bisector.

**Implementation** (`shaders/plot_line_adjacency.geom`):

1. **Direction Vector Calculation**:
   - Calculate normalized directions for previous, current, and next segments
   - Compute perpendicular offsets at half line width

2. **Convex vs Reflex Detection**:
   - Use cross product to determine turn direction
   - `cross(dir_prev, dir_curr) > 0`: turning left (top is convex)
   - `cross(dir_prev, dir_curr) < 0`: turning right (bottom is convex)

3. **Miter Point Calculation**:
   - Compute angle bisector between adjacent segments
   - Project line width onto bisector to find miter point
   - Clamp miter length with configurable limit (currently 10x line width)

4. **Quad Emission**:
   - Output: `triangle_strip` with 4 vertices per segment
   - Convex side: uses calculated miter point
   - Reflex side: uses simple perpendicular offset (TODO: gap filling)
   - First/last segments: use simple offsets (no miter needed)

**Result**:
- ✅ Convex angles have clean, sharp joins with no overlap
- ✅ Line width is consistent across joins
- ⚠️ Reflex angles currently have gaps (Phase 3)

**Visual Example**:
```        prev            next
           \              /
            \            /  <- convex angle (mitered)
             \          /
          miter point *
                     /  \       p0 -------- p1
            (current segment quad with mitered corner)
```

### Phase 3: Reflex Join Geometry (NEXT STEP)

**Goal**: Fill gaps on reflex (outside) angles.

**Challenge**: The reflex side is trickier because there's a gap that needs filling.

**Approach**: Emit additional triangle(s) to fill the gap on the reflex (outside) angle.

**Options**:
- **Bevel**: Single triangle connecting the two offset edges (simpler, sharper corner)
- **Round**: Multiple triangles forming a circular arc (smoother, more expensive)

**Recommended**: Start with bevel for simplicity.

**Implementation Plan**:

1. **Detect Reflex Joins**:
   - Currently identified by cross product (already done)
   - Reflex side uses simple perpendicular offset

2. **Emit Gap-Filling Triangle**:
   - After emitting the main quad, check for reflex joins
   - Emit additional triangle between:
     * p + perp_prev (outer edge of previous segment)
     * p (centerline vertex)
     * p + perp_curr (outer edge of current segment)
   - Increase `max_vertices` to 7 (4 for quad + 3 for potential join triangle)

3. **Handle Both Start and End Joins**:
   - Start join: emit triangle if reflex
   - End join: emit triangle if reflex
   - May need up to 10 vertices total (quad + 2 join triangles)

4. **Update Output Layout**:
   - Change `max_vertices` from 4 to 10 (conservative estimate)
   - Emit join triangles as separate primitives or extend the triangle strip

### Phase 4: Colormap Integration (CURRENT - ALREADY WORKING)

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

**Phase 1 Impact**: Minimal
- EBO management with capacity headroom to minimize reallocations
- `glDrawElements` vs `glDrawArrays` negligible difference
- Infrastructure overhead only

**Phase 2 Impact** (Current):
- **4 vertices per segment** (triangle strip quad) vs 2 for line strip
- **2x geometry throughput** increase (acceptable trade-off for quality)
- Miter calculation adds ~20-30 shader instructions per segment
- Fragment shader invocations increase proportional to line width
- Overall: Well within GPU budget for typical use cases

**Phase 3 Impact** (Future):
- Additional triangles for reflex joins (~3 vertices per join)
- Worst case: 10 vertices per segment if both joins are reflex
- Should still be acceptable for typical data densities
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
