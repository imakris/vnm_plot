#version 430

// ====================================================================================
// ADJACENCY-AWARE LINE GEOMETRY SHADER FOR COLORMAP_LINE
// ====================================================================================
//
// OBJECTIVE:
//   Fix visual quality issues where line joins/corners appear inconsistent across
//   different LODs (Levels of Detail). The original implementation rendered each
//   segment independently without neighbor information, causing poor join quality.
//
// APPROACH:
//   Use OpenGL's adjacency primitives (GL_LINE_STRIP_ADJACENCY) to provide geometric
//   context for each line segment. This shader receives 4 vertices per segment,
//   including the neighboring vertices needed to calculate proper joins.
//
// CURRENT STATUS (Phase 1 - Infrastructure):
//   This is a conservative first step that establishes the adjacency framework
//   without changing the visual output. We receive adjacency data and transform it,
//   but currently emit simple line segments just like the non-adjacency version.
//
// NEXT STEPS (Phase 2 - Join Geometry):
//   Future enhancements will use the adjacency information to:
//   1. Calculate angles between adjacent segments
//   2. Generate trapezoid/triangle geometry for proper joins (bevel or miter)
//   3. Emit triangle strips instead of line strips for filled joins
//   4. Handle edge cases (first/last segments, acute angles, degenerate segments)
//
// INPUT VERTEX LAYOUT:
//   For each line segment, we receive 4 vertices via lines_adjacency:
//
//   gs_in[0]: PREVIOUS vertex (before current segment)
//             - Used to calculate incoming join angle
//             - For first segment: duplicated from gs_in[1]
//
//   gs_in[1]: CURRENT SEGMENT START
//             - The actual start point of this line segment
//
//   gs_in[2]: CURRENT SEGMENT END
//             - The actual end point of this line segment
//
//   gs_in[3]: NEXT vertex (after current segment)
//             - Used to calculate outgoing join angle
//             - For last segment: duplicated from gs_in[2]
//
// VISUAL REPRESENTATION:
//
//        v0 (prev)          v3 (next)
//            *                 *
//             \               /
//              \             /
//               \           /
//                v1-------v2
//              (seg start)(seg end)
//
//   The shader processes the segment v1→v2, but has access to v0 and v3
//   to understand the geometric context for proper join rendering.
//
// ====================================================================================

layout(location =  0) uniform mat4    pmv;
layout(location =  1) uniform double  t_min;
layout(location =  2) uniform double  t_max;
layout(location =  3) uniform float   v_min;
layout(location =  4) uniform float   v_max;
layout(location =  5) uniform double  width;
layout(location =  6) uniform double  height;
layout(location =  7) uniform float   y_offset;
layout(location =  8) uniform vec4    color;
layout(location =  9) uniform bool    snap_to_pixels;

// Input: lines_adjacency provides 4 vertices per invocation
layout (lines_adjacency) in;

// Output: For Phase 1, we emit simple lines (2 vertices)
// TODO (Phase 2): Change to triangle_strip with max_vertices = 6-8 for join geometry
layout (line_strip, max_vertices = 2) out;

// Input from vertex shader (4 vertices per invocation due to lines_adjacency)
in Sample {
    double t;      // Timestamp (horizontal position in data space)
    float v;       // Value (vertical position in data space)
    flat int status;  // Status flags (currently unused)
} gs_in[];

// Output to fragment shader
out vec4 line_color;

// ====================================================================================
// HELPER: emit_point
// ====================================================================================
// Transforms a point from screen pixel coordinates to clip space and emits it.
//
// Parameters:
//   xd, yd: Position in screen pixel coordinates (origin at bottom-left)
//
// Behavior:
//   - Optionally snaps to pixel centers if snap_to_pixels is enabled
//   - Transforms to clip space using the PMV matrix
//   - Emits the vertex with the current color
//
void emit_point(double xd, double yd)
{
    if (snap_to_pixels) {
        // Snap to pixel centers (0.5, 1.5, 2.5, ...) to avoid subpixel rendering
        // artifacts and ensure consistent appearance across segments.
        xd = floor(xd) + 0.5;
        yd = floor(yd) + 0.5;
    }

    // Transform from screen pixel space to clip space
    gl_Position = pmv * vec4(float(xd), float(yd), 0.0, 1.0);
    line_color = color;
    EmitVertex();
}

// ====================================================================================
// MAIN SHADER LOGIC
// ====================================================================================
void main()
{
    // ================================================================================
    // STEP 1: Calculate safe denominators for data-to-screen transformation
    // ================================================================================
    // Ensure we never divide by zero, even for degenerate cases where the viewport
    // covers zero time or value range.
    double rt = max(t_max - t_min, 1e-30);  // Time range
    double rv = max(double(v_max - v_min), 1e-30);  // Value range

    // ================================================================================
    // STEP 2: Transform all 4 adjacency vertices from data space to screen space
    // ================================================================================
    // We transform all vertices to screen pixel coordinates. Even though we don't
    // use the adjacency vertices (prev/next) for rendering yet, we compute them
    // here to prepare for Phase 2 implementation.
    //
    // Data space: (timestamp, value)
    // Screen space: (pixel_x, pixel_y) where (0,0) is bottom-left
    //
    // Transform formula:
    //   x = width * (t - t_min) / (t_max - t_min)
    //   y = height * (1 - (v - v_min) / (v_max - v_min)) + y_offset
    //
    // Note: Y is flipped (1 - ...) because data space has higher values at top,
    //       but screen space has higher Y values at top as well. The y_offset
    //       accounts for multi-panel layouts.

    // PREVIOUS vertex (gs_in[0]) - Used for incoming join angle calculation
    // For the first segment in the strip, this is duplicated from gs_in[1]
    double x_prev = width  *      (gs_in[0].t - t_min) / rt;
    double y_prev = height * (1.0 - (double(gs_in[0].v) - double(v_min)) / rv) + double(y_offset);

    // SEGMENT START (gs_in[1]) - The actual start of the current segment
    double x0 = width  *      (gs_in[1].t - t_min) / rt;
    double y0 = height * (1.0 - (double(gs_in[1].v) - double(v_min)) / rv) + double(y_offset);

    // SEGMENT END (gs_in[2]) - The actual end of the current segment
    double x1 = width  *      (gs_in[2].t - t_min) / rt;
    double y1 = height * (1.0 - (double(gs_in[2].v) - double(v_min)) / rv) + double(y_offset);

    // NEXT vertex (gs_in[3]) - Used for outgoing join angle calculation
    // For the last segment in the strip, this is duplicated from gs_in[2]
    double x_next = width  *      (gs_in[3].t - t_min) / rt;
    double y_next = height * (1.0 - (double(gs_in[3].v) - double(v_min)) / rv) + double(y_offset);

    // ================================================================================
    // STEP 3: Emit geometry (PHASE 1 - Simple line segment)
    // ================================================================================
    // For this conservative first implementation, we simply emit the segment as
    // a basic line from v1 to v2, ignoring the adjacency information.
    //
    // TODO (Phase 2): Replace this with proper join geometry:
    //
    //   1. Calculate direction vectors:
    //      vec2 dir_in  = normalize(vec2(x0 - x_prev, y0 - y_prev));  // incoming
    //      vec2 dir_seg = normalize(vec2(x1 - x0, y1 - y0));          // current
    //      vec2 dir_out = normalize(vec2(x_next - x1, y_next - y1));  // outgoing
    //
    //   2. Calculate perpendicular offsets for line width:
    //      vec2 perp_in  = vec2(-dir_in.y,  dir_in.x)  * line_width * 0.5;
    //      vec2 perp_seg = vec2(-dir_seg.y, dir_seg.x) * line_width * 0.5;
    //      vec2 perp_out = vec2(-dir_out.y, dir_out.x) * line_width * 0.5;
    //
    //   3. Emit trapezoid vertices for the segment with proper joins:
    //      - Start join (using dir_in and dir_seg)
    //      - Segment body
    //      - End join (using dir_seg and dir_out)
    //
    //   4. Handle edge cases:
    //      - First segment: x_prev == x0 && y_prev == y0 → use dir_seg only
    //      - Last segment:  x_next == x1 && y_next == y1 → use dir_seg only
    //      - Degenerate:    x0 == x1 && y0 == y1 → skip emission
    //
    //   5. Change output layout to:
    //      layout (triangle_strip, max_vertices = 8) out;
    //
    // ================================================================================

    // Phase 1: Emit simple line segment (no joins yet)
    emit_point(x0, y0);  // Segment start
    emit_point(x1, y1);  // Segment end

    EndPrimitive();
}
