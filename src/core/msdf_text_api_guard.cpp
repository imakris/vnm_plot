#include <vnm_msdf_text/msdf_text.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using atlas_t        = vnm::msdf_text::atlas_t;
using build_result_t = vnm::msdf_text::build_result_t;
using build_status_t = vnm::msdf_text::Build_status;
using glyph_t        = vnm::msdf_text::glyph_t;
using metrics_t      = vnm::msdf_text::font_metrics_px_t;
using options_t      = vnm::msdf_text::options_t;
using text_vertex_t  = vnm::msdf_text::text_vertex_t;

static_assert(std::is_same_v<decltype(build_result_t::status), build_status_t>);
static_assert(std::is_same_v<decltype(atlas_t::font_metrics), metrics_t>);
static_assert(std::is_same_v<decltype(metrics_t::ascender), float>);
static_assert(std::is_same_v<decltype(metrics_t::descender), float>);
static_assert(std::is_same_v<decltype(metrics_t::line_height), float>);
static_assert(std::is_same_v<decltype(metrics_t::em_size), float>);

static_assert(std::is_same_v<decltype(glyph_t::advance_x), float>);
static_assert(std::is_same_v<decltype(glyph_t::plane_left), float>);
static_assert(std::is_same_v<decltype(glyph_t::plane_bottom), float>);
static_assert(std::is_same_v<decltype(glyph_t::plane_right), float>);
static_assert(std::is_same_v<decltype(glyph_t::plane_top), float>);
static_assert(std::is_same_v<decltype(glyph_t::uv_left), float>);
static_assert(std::is_same_v<decltype(glyph_t::uv_bottom), float>);
static_assert(std::is_same_v<decltype(glyph_t::uv_right), float>);
static_assert(std::is_same_v<decltype(glyph_t::uv_top), float>);

static_assert(std::is_same_v<decltype(atlas_t::pixel_height), int>);
static_assert(std::is_same_v<decltype(atlas_t::atlas_size), int>);
static_assert(std::is_same_v<decltype(atlas_t::px_range), float>);
static_assert(std::is_same_v<decltype(atlas_t::zero_advance_px), float>);
static_assert(std::is_same_v<decltype(atlas_t::zero_advance_available), bool>);

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

using measure_advance_fn_t = float (*)(
    const atlas_t&,
    std::string_view);

using append_quads_fn_t = void (*)(
    const atlas_t&,
    std::string_view,
    float,
    float,
    std::vector<text_vertex_t>&,
    std::vector<std::uint32_t>*);

static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::build_font_atlas),
    build_font_atlas_fn_t>);
static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::measure_text_advance_px),
    measure_advance_fn_t>);
static_assert(std::is_same_v<
    decltype(&vnm::msdf_text::append_text_quads),
    append_quads_fn_t>);

} // anonymous namespace
