#version 430

// ====================================================================================
// ADJACENCY-AWARE LINE GEOMETRY SHADER FOR COLORMAP_LINE
// ====================================================================================
//
// SCOPE: This shader implements adjacency-aware join geometry ONLY.
//        Colormap sampling is NOT implemented - line renders as solid color.
//
// PROBLEM:
//   The original COLORMAP_LINE rendering created per-segment quads with no adjacency
//   information. This caused line joins to look inconsistent across different zoom
//   levels (LODs):
//   - Convex angles: Overlapping quads created visual artifacts
//   - Reflex angles: Gaps appeared between segments
//   - LOD changes: Same corner would look different at different zoom levels
//
// SOLUTION:
//   Use OpenGL's adjacency primitives (GL_LINE_STRIP_ADJACENCY) to access neighbor
//   vertices, enabling proper join geometry:
//   - Convex joins: Miter points where outer edges meet (eliminates overlap)
//   - Reflex joins: Single-subdivision using bisector point at half_width (fills gaps)
//
// NOTE: Signal values (gs_in[].v) are available but not currently used.
//       To implement colormap: pass signal value to fragment shader and sample texture.
//
// CURRENT STATUS (Phase 2 - Complete Join Geometry):
//   - Convex angles: Calculate miter points where outer edges meet (no overlap)
//   - Reflex angles: Single-subdivision join with bisector point at half_width
//   - Emit triangle strip for main segment quad
//   - Emit additional triangles for reflex joins (one subdivision toward round)
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
//               \           /     <- convex angle (miter applied here)
//                v1-------v2
//              (seg start)(seg end)
//
//   The shader processes the segment v1→v2, emitting a quad with mitered corners
//   where the outer edges meet on the angle bisector.
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
layout(location = 10) uniform float   u_line_px;  // Line width in pixels

// Input: lines_adjacency provides 4 vertices per invocation
layout (lines_adjacency) in;

// Output: Triangle strip for thick line segments with mitered joins
// Max vertices: 4 (quad) + 6 (start reflex: 2 triangles) + 6 (end reflex: 2 triangles) = 16
layout (triangle_strip, max_vertices = 16) out;

// Input from vertex shader (4 vertices per invocation due to lines_adjacency)
in Sample {
    double t;      // Timestamp (horizontal position in data space)
    float v;       // Value (vertical position in data space)
    flat int status;  // Status flags (currently unused)
} gs_in[];

// Output to fragment shader
out vec4 line_color;

// ====================================================================================
// HELPER: Calculate miter point for convex join
// ====================================================================================
// Calculates where the outer edges of two line segments intersect when forming
// a convex (inside) angle. This point lies on the angle bisector.
//
// Parameters:
//   center: The vertex where the two segments meet (v1)
//   dir_in: Normalized direction of incoming segment (v0→v1)
//   dir_out: Normalized direction of outgoing segment (v1→v2)
//   perp_in: Perpendicular offset for incoming segment
//   perp_out: Perpendicular offset for outgoing segment
//   line_width: Width of the line in pixels
//
// Returns:
//   The miter point where outer edges intersect, or simple offset if parallel
//
vec2 calculate_miter_point(vec2 center, vec2 dir_in, vec2 dir_out,
                           vec2 perp_in, vec2 perp_out, float line_width)
{
    // Calculate the miter direction (bisector of the angle)
    vec2 tangent = normalize(dir_in + dir_out);

    // Perpendicular to the tangent (along the bisector)
    vec2 miter = vec2(-tangent.y, tangent.x);

    // Calculate miter length
    // The miter extends further from the centerline as the angle gets sharper
    float dot_prod = dot(miter, perp_out);

    // Avoid division by zero for parallel segments
    if (abs(dot_prod) < 0.001) {
        return center + perp_out;
    }

    // Miter length is line_width/2 divided by the projection
    float miter_length = (line_width * 0.5) / dot_prod;

    // Limit miter length to avoid extremely long spikes on acute angles
    // TODO: Make this configurable, standard is 4.0 or so
    const float miter_limit = 10.0;
    miter_length = clamp(miter_length, -line_width * miter_limit, line_width * miter_limit);

    return center + miter * miter_length;
}

// ====================================================================================
// HELPER: Calculate bisector point for reflex join
// ====================================================================================
// For reflex (outside) angles, calculates a point on the angle bisector at
// distance half_width from the center. This is used for single-subdivision joins.
//
// Parameters:
//   center: The vertex where the two segments meet
//   dir_in: Normalized direction of incoming segment
//   dir_out: Normalized direction of outgoing segment
//   half_width: Half the line width
//   sign: +1.0 for top side, -1.0 for bottom side
//   perp_curr: Fallback perpendicular (for near-straight angles)
//
// Returns:
//   Point on the bisector at distance half_width from center
//
// Numerical Stability:
//   For angles near 180°, dir_in + dir_out has very small magnitude.
//   We detect this and fall back to simple perpendicular offset.
//
vec2 calculate_reflex_bisector_point(vec2 center, vec2 dir_in, vec2 dir_out,
                                     float half_width, float sign, vec2 perp_curr)
{
    // Calculate the bisector direction (average of the two directions)
    vec2 tangent_unnormalized = dir_in + dir_out;
    float tangent_length = length(tangent_unnormalized);

    // For near-180° angles, the sum has very small magnitude
    // Normalizing would amplify floating-point errors
    // Fall back to perpendicular of current segment
    if (tangent_length < 0.01) {
        return center + perp_curr * sign;
    }

    vec2 tangent = tangent_unnormalized / tangent_length;

    // Perpendicular to the bisector (this points toward the outer edge)
    vec2 bisector_perp = vec2(-tangent.y, tangent.x);

    // Place point at half_width distance along the bisector
    // Sign determines which side (top or bottom)
    return center + bisector_perp * half_width * sign;
}

// ====================================================================================
// HELPER: emit_vertex
// ====================================================================================
// Emits a vertex with optional pixel snapping
//
void emit_vertex(vec2 pos)
{
    if (snap_to_pixels) {
        pos = floor(pos) + vec2(0.5);
    }
    gl_Position = pmv * vec4(pos, 0.0, 1.0);
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
    double rt = max(t_max - t_min, 1e-30);  // Time range
    double rv = max(double(v_max - v_min), 1e-30);  // Value range

    // ================================================================================
    // STEP 2: Transform all 4 adjacency vertices from data space to screen space
    // ================================================================================

    // PREVIOUS vertex (gs_in[0])
    vec2 p_prev = vec2(
        float(width  *      (gs_in[0].t - t_min) / rt),
        float(height * (1.0 - (double(gs_in[0].v) - double(v_min)) / rv) + double(y_offset))
    );

    // SEGMENT START (gs_in[1])
    vec2 p0 = vec2(
        float(width  *      (gs_in[1].t - t_min) / rt),
        float(height * (1.0 - (double(gs_in[1].v) - double(v_min)) / rv) + double(y_offset))
    );

    // SEGMENT END (gs_in[2])
    vec2 p1 = vec2(
        float(width  *      (gs_in[2].t - t_min) / rt),
        float(height * (1.0 - (double(gs_in[2].v) - double(v_min)) / rv) + double(y_offset))
    );

    // NEXT vertex (gs_in[3])
    vec2 p_next = vec2(
        float(width  *      (gs_in[3].t - t_min) / rt),
        float(height * (1.0 - (double(gs_in[3].v) - double(v_min)) / rv) + double(y_offset))
    );

    // ================================================================================
    // STEP 3: Calculate direction vectors and perpendiculars
    // ================================================================================

    float line_width = u_line_px;
    float half_width = line_width * 0.5;

    // Direction of current segment
    vec2 dir_curr = p1 - p0;
    float len_curr = length(dir_curr);

    // Skip degenerate segments
    if (len_curr < 0.001) {
        return;
    }

    dir_curr = dir_curr / len_curr;  // Normalize

    // Perpendicular to current segment (rotated 90° CCW)
    vec2 perp_curr = vec2(-dir_curr.y, dir_curr.x) * half_width;

    // ================================================================================
    // STEP 4: Calculate start join (v1 with previous segment)
    // ================================================================================

    vec2 offset_start_top, offset_start_bottom;
    bool start_has_reflex = false;
    bool start_reflex_is_top = false;
    vec2 dir_prev = vec2(0.0);

    // Check if this is the first segment (prev == start)
    if (length(p_prev - p0) < 0.001) {
        // First segment: no previous, use simple perpendicular offset
        offset_start_top = perp_curr;
        offset_start_bottom = -perp_curr;
    } else {
        // Calculate previous segment direction
        dir_prev = p0 - p_prev;
        float len_prev = length(dir_prev);

        if (len_prev > 0.001) {
            dir_prev = dir_prev / len_prev;
            vec2 perp_prev = vec2(-dir_prev.y, dir_prev.x) * half_width;

            // Determine which side is convex using cross product
            // cross(dir_prev, dir_curr) > 0 means turning left (CCW)
            float cross_prod = dir_prev.x * dir_curr.y - dir_prev.y * dir_curr.x;

            if (cross_prod > 0.001) {
                // Turning left: top side is convex (needs miter), bottom is reflex
                vec2 miter_top = calculate_miter_point(p0, dir_prev, dir_curr, perp_prev, perp_curr, line_width);
                offset_start_top = miter_top - p0;
                offset_start_bottom = -perp_curr;  // Simple offset on reflex side
                start_has_reflex = true;
                start_reflex_is_top = false;  // Bottom is reflex
            } else if (cross_prod < -0.001) {
                // Turning right: bottom side is convex (needs miter), top is reflex
                vec2 miter_bottom = calculate_miter_point(p0, dir_prev, dir_curr, -perp_prev, -perp_curr, line_width);
                offset_start_top = perp_curr;  // Simple offset on reflex side
                offset_start_bottom = miter_bottom - p0;
                start_has_reflex = true;
                start_reflex_is_top = true;  // Top is reflex
            } else {
                // Straight line: use simple offsets
                offset_start_top = perp_curr;
                offset_start_bottom = -perp_curr;
            }
        } else {
            offset_start_top = perp_curr;
            offset_start_bottom = -perp_curr;
        }
    }

    // ================================================================================
    // STEP 5: Calculate end join (v2 with next segment)
    // ================================================================================

    vec2 offset_end_top, offset_end_bottom;
    bool end_has_reflex = false;
    bool end_reflex_is_top = false;
    vec2 dir_next = vec2(0.0);

    // Check if this is the last segment (end == next)
    if (length(p_next - p1) < 0.001) {
        // Last segment: no next, use simple perpendicular offset
        offset_end_top = perp_curr;
        offset_end_bottom = -perp_curr;
    } else {
        // Calculate next segment direction
        dir_next = p_next - p1;
        float len_next = length(dir_next);

        if (len_next > 0.001) {
            dir_next = dir_next / len_next;
            vec2 perp_next = vec2(-dir_next.y, dir_next.x) * half_width;

            // Determine which side is convex
            float cross_prod = dir_curr.x * dir_next.y - dir_curr.y * dir_next.x;

            if (cross_prod > 0.001) {
                // Turning left: top side is convex
                vec2 miter_top = calculate_miter_point(p1, dir_curr, dir_next, perp_curr, perp_next, line_width);
                offset_end_top = miter_top - p1;
                offset_end_bottom = -perp_curr;
                end_has_reflex = true;
                end_reflex_is_top = false;  // Bottom is reflex
            } else if (cross_prod < -0.001) {
                // Turning right: bottom side is convex
                vec2 miter_bottom = calculate_miter_point(p1, dir_curr, dir_next, -perp_curr, -perp_next, line_width);
                offset_end_top = perp_curr;
                offset_end_bottom = miter_bottom - p1;
                end_has_reflex = true;
                end_reflex_is_top = true;  // Top is reflex
            } else {
                // Straight line
                offset_end_top = perp_curr;
                offset_end_bottom = -perp_curr;
            }
        } else {
            offset_end_top = perp_curr;
            offset_end_bottom = -perp_curr;
        }
    }

    // ================================================================================
    // STEP 6: Emit triangle strip forming the segment quad with mitered corners
    // ================================================================================
    // Triangle strip order: top-left, bottom-left, top-right, bottom-right
    // This forms a quad (two triangles) for the line segment

    emit_vertex(p0 + offset_start_top);      // Top-left
    emit_vertex(p0 + offset_start_bottom);   // Bottom-left
    emit_vertex(p1 + offset_end_top);        // Top-right
    emit_vertex(p1 + offset_end_bottom);     // Bottom-right

    EndPrimitive();

    // ================================================================================
    // STEP 7: Emit reflex join triangles (single-subdivision toward round)
    // ================================================================================
    // For reflex angles, emit two triangles that fill the gap using a bisector point
    // at half_width distance from the centerline. This creates a slightly rounded
    // appearance (one subdivision toward a fully round join).

    if (start_has_reflex) {
        // Calculate the bisector point at half_width distance
        float sign = start_reflex_is_top ? 1.0 : -1.0;
        vec2 bisector_point = calculate_reflex_bisector_point(p0, dir_prev, dir_curr, half_width, sign, perp_curr);

        // Get the perpendicular offset for the previous segment
        vec2 perp_prev = vec2(-dir_prev.y, dir_prev.x) * half_width;

        if (start_reflex_is_top) {
            // Top is reflex: emit two triangles to fill the gap
            // Triangle 1: previous segment top → bisector point → center
            emit_vertex(p0 + perp_prev);
            emit_vertex(bisector_point);
            emit_vertex(p0);
            EndPrimitive();

            // Triangle 2: center → bisector point → current segment top
            emit_vertex(p0);
            emit_vertex(bisector_point);
            emit_vertex(p0 + offset_start_top);
            EndPrimitive();
        } else {
            // Bottom is reflex: emit two triangles to fill the gap
            // Triangle 1: previous segment bottom → bisector point → center
            emit_vertex(p0 - perp_prev);
            emit_vertex(bisector_point);
            emit_vertex(p0);
            EndPrimitive();

            // Triangle 2: center → bisector point → current segment bottom
            emit_vertex(p0);
            emit_vertex(bisector_point);
            emit_vertex(p0 + offset_start_bottom);
            EndPrimitive();
        }
    }

    if (end_has_reflex) {
        // Calculate the bisector point at half_width distance
        float sign = end_reflex_is_top ? 1.0 : -1.0;
        vec2 bisector_point = calculate_reflex_bisector_point(p1, dir_curr, dir_next, half_width, sign, perp_curr);

        // Get the perpendicular offset for the next segment
        vec2 perp_next = vec2(-dir_next.y, dir_next.x) * half_width;

        if (end_reflex_is_top) {
            // Top is reflex: emit two triangles to fill the gap
            // Triangle 1: current segment top → bisector point → center
            emit_vertex(p1 + offset_end_top);
            emit_vertex(bisector_point);
            emit_vertex(p1);
            EndPrimitive();

            // Triangle 2: center → bisector point → next segment top
            emit_vertex(p1);
            emit_vertex(bisector_point);
            emit_vertex(p1 + perp_next);
            EndPrimitive();
        } else {
            // Bottom is reflex: emit two triangles to fill the gap
            // Triangle 1: current segment bottom → bisector point → center
            emit_vertex(p1 + offset_end_bottom);
            emit_vertex(bisector_point);
            emit_vertex(p1);
            EndPrimitive();

            // Triangle 2: center → bisector point → next segment bottom
            emit_vertex(p1);
            emit_vertex(bisector_point);
            emit_vertex(p1 - perp_next);
            EndPrimitive();
        }
    }
}
