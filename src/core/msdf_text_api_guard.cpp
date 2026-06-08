#include <vnm_msdf_text/msdf_text.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Compile-time canary pinning the vnm_msdf_text API surface that font_renderer
// relies on. This is the scale-independent contract: the atlas stores geometry,
// advances, kerning, and metrics in font units, and draw-size values are derived
// per draw pixel height by the scaling helpers. If the upstream library changes
// these shapes, this translation unit fails to compile here rather than silently
// at a call site.

namespace {

using atlas_t         = vnm::msdf_text::atlas_t;
using build_result_t  = vnm::msdf_text::build_result_t;
using build_status_t  = vnm::msdf_text::Build_status;
using glyph_t         = vnm::msdf_text::glyph_t;
using scaled_glyph_t  = vnm::msdf_text::scaled_glyph_t;
using metrics_px_t    = vnm::msdf_text::font_metrics_px_t;
using metrics_units_t = vnm::msdf_text::font_metrics_units_t;
using options_t       = vnm::msdf_text::options_t;
using text_vertex_t   = vnm::msdf_text::text_vertex_t;

static_assert(std::is_same_v<decltype(build_result_t::status), build_status_t>);

// Scale-independent per-glyph geometry: font-unit bounds/advance plus a
// visibility flag and bake-determined UVs.
static_assert(std::is_same_v<decltype(glyph_t::advance_units), float>);
static_assert(std::is_same_v<decltype(glyph_t::bounds_left_units), float>);
static_assert(std::is_same_v<decltype(glyph_t::bounds_bottom_units), float>);
static_assert(std::is_same_v<decltype(glyph_t::bounds_right_units), float>);
static_assert(std::is_same_v<decltype(glyph_t::bounds_top_units), float>);
static_assert(std::is_same_v<decltype(glyph_t::visible), bool>);
static_assert(std::is_same_v<decltype(glyph_t::uv_left), float>);
static_assert(std::is_same_v<decltype(glyph_t::uv_bottom), float>);
static_assert(std::is_same_v<decltype(glyph_t::uv_right), float>);
static_assert(std::is_same_v<decltype(glyph_t::uv_top), float>);

// Draw-size geometry produced by scaled_glyph(): screen Y-down plane rectangle.
static_assert(std::is_same_v<decltype(scaled_glyph_t::advance_x), float>);
static_assert(std::is_same_v<decltype(scaled_glyph_t::plane_left), float>);
static_assert(std::is_same_v<decltype(scaled_glyph_t::plane_bottom), float>);
static_assert(std::is_same_v<decltype(scaled_glyph_t::plane_right), float>);
static_assert(std::is_same_v<decltype(scaled_glyph_t::plane_top), float>);
static_assert(std::is_same_v<decltype(scaled_glyph_t::uv_left), float>);
static_assert(std::is_same_v<decltype(scaled_glyph_t::uv_bottom), float>);
static_assert(std::is_same_v<decltype(scaled_glyph_t::uv_right), float>);
static_assert(std::is_same_v<decltype(scaled_glyph_t::uv_top), float>);

// Font metrics: stored in units on the atlas, scaled to pixels by
// scaled_font_metrics().
static_assert(std::is_same_v<decltype(atlas_t::font_metrics_units), metrics_units_t>);
static_assert(std::is_same_v<decltype(metrics_units_t::ascender), float>);
static_assert(std::is_same_v<decltype(metrics_units_t::descender), float>);
static_assert(std::is_same_v<decltype(metrics_units_t::line_height), float>);
static_assert(std::is_same_v<decltype(metrics_units_t::em_size), float>);
static_assert(std::is_same_v<decltype(metrics_px_t::ascender), float>);
static_assert(std::is_same_v<decltype(metrics_px_t::descender), float>);
static_assert(std::is_same_v<decltype(metrics_px_t::line_height), float>);
static_assert(std::is_same_v<decltype(metrics_px_t::em_size), float>);

// Atlas fields font_renderer serializes and feeds to the scaling helpers.
static_assert(std::is_same_v<decltype(atlas_t::baked_pixel_height), int>);
static_assert(std::is_same_v<decltype(atlas_t::atlas_size), int>);
static_assert(std::is_same_v<decltype(atlas_t::atlas_px_range), double>);
static_assert(std::is_same_v<decltype(atlas_t::bitmap_scale), double>);
static_assert(std::is_same_v<decltype(atlas_t::sharpness_bias), float>);
static_assert(std::is_same_v<decltype(atlas_t::zero_advance_units), float>);
static_assert(std::is_same_v<decltype(atlas_t::zero_advance_available), bool>);

// Types that define the on-disk cache byte layout font_renderer serializes: the
// kerning key width, the glyph/kerning map key and value types, and the bitmap
// element type. An upstream change to any of these silently alters the cache
// format, so pin them here rather than let only the runtime version bump mask it.
static_assert(std::is_same_v<vnm::msdf_text::kerning_key_t, std::uint64_t>);
static_assert(std::is_same_v<
    decltype(atlas_t::kerning_units),
    std::unordered_map<vnm::msdf_text::kerning_key_t, float>>);
static_assert(std::is_same_v<
    decltype(atlas_t::glyphs),
    std::unordered_map<char32_t, glyph_t>>);
static_assert(std::is_same_v<decltype(atlas_t::rgba), std::vector<std::uint8_t>>);

static_assert(std::is_standard_layout_v<text_vertex_t>);
static_assert(std::is_same_v<decltype(text_vertex_t::x), float>);
static_assert(std::is_same_v<decltype(text_vertex_t::y), float>);
static_assert(std::is_same_v<decltype(text_vertex_t::s), float>);
static_assert(std::is_same_v<decltype(text_vertex_t::t), float>);
static_assert(std::is_same_v<decltype(text_vertex_t::s_min), float>);
static_assert(std::is_same_v<decltype(text_vertex_t::t_min), float>);
static_assert(std::is_same_v<decltype(text_vertex_t::s_max), float>);
static_assert(std::is_same_v<decltype(text_vertex_t::t_max), float>);
static_assert(offsetof(text_vertex_t, x)     == 0 * sizeof(float));
static_assert(offsetof(text_vertex_t, y)     == 1 * sizeof(float));
static_assert(offsetof(text_vertex_t, s)     == 2 * sizeof(float));
static_assert(offsetof(text_vertex_t, t)     == 3 * sizeof(float));
static_assert(offsetof(text_vertex_t, s_min) == 4 * sizeof(float));
static_assert(offsetof(text_vertex_t, t_min) == 5 * sizeof(float));
static_assert(offsetof(text_vertex_t, s_max) == 6 * sizeof(float));
static_assert(offsetof(text_vertex_t, t_max) == 7 * sizeof(float));
static_assert(sizeof(text_vertex_t) == 8 * sizeof(float));

static_assert(build_status_t::FAILURE != build_status_t::SUCCESS);
static_assert(
    build_status_t::PARTIAL_SUCCESS != build_status_t::FAILURE &&
    build_status_t::PARTIAL_SUCCESS != build_status_t::SUCCESS);

using build_font_atlas_fn_t = build_result_t (*)(
    const std::uint8_t*,
    std::size_t,
    int,
    std::span<const char32_t>,
    const options_t&,
    const vnm::msdf_text::log_callback_t&);

// Measurement and layout take a draw pixel height and apply scaling internally.
using measure_advance_fn_t = float (*)(
    const atlas_t&,
    int,
    std::string_view);

using append_quads_fn_t = void (*)(
    const atlas_t&,
    int,
    std::string_view,
    float,
    float,
    std::vector<text_vertex_t>&,
    std::vector<std::uint32_t>*);

// Scaling helpers font_renderer uses to derive draw-size geometry, metrics, and
// the shader px_range from the font-unit atlas.
using scaled_glyph_fn_t = scaled_glyph_t (*)(
    const atlas_t&,
    const glyph_t&,
    int);

using scaled_font_metrics_fn_t = metrics_px_t (*)(
    const atlas_t&,
    int);

using px_range_fn_t = float (*)(
    const atlas_t&,
    int);

static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::build_font_atlas),
    build_font_atlas_fn_t>);
static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::measure_text_advance_px),
    measure_advance_fn_t>);
static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::append_text_quads),
    append_quads_fn_t>);
static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::scaled_glyph),
    scaled_glyph_fn_t>);
static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::scaled_font_metrics),
    scaled_font_metrics_fn_t>);
static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::px_range_for_pixel_height),
    px_range_fn_t>);

} // anonymous namespace
