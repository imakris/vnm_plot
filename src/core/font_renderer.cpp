#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/asset_loader.h>
#include "platform_paths.h"
#include "sha256.h"
#include "tls_registry.h"
#include "rhi_helpers.h"

#include <glm/gtc/type_ptr.hpp>
#include <vnm_msdf_text/msdf_text.h>

#include <QFile>
#include <QImage>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace vnm::plot {

namespace {

constexpr std::uint32_t k_cache_version = 1;
constexpr double k_min_atlas_font_size = 48.0;
constexpr float k_atlas_px_range = 10.0f;
constexpr float k_sharpness_bias = 2.5f;
constexpr int k_atlas_texture_size = 2048;

std::atomic<bool> s_disk_cache_enabled{true};

} // anonymous namespace

// -----------------------------------------------------------------------------
// Font Cache Configuration
// -----------------------------------------------------------------------------

void set_font_disk_cache_enabled(bool enabled)
{
    s_disk_cache_enabled.store(enabled, std::memory_order_relaxed);
}

bool font_disk_cache_enabled()
{
    return s_disk_cache_enabled.load(std::memory_order_relaxed);
}

namespace {

struct vertex_buffer_t
{
    std::vector<float> vertex_data; // 8 floats per vertex: position, tex coord, tex bounds
    std::vector<std::uint32_t> index_data;
};

using msdf_atlas_t = vnm::msdf_text::atlas_t;
using msdf_glyph_t = vnm::msdf_text::glyph_t;
using msdf_kerning_key_t = vnm::msdf_text::kerning_key_t;
using text_vertex_t = vnm::msdf_text::text_vertex_t;

static_assert(sizeof(text_vertex_t) == 8 * sizeof(float), "MSDF text vertex layout");

vertex_buffer_t* vertex_buffer_new(const char*)
{
    return new vertex_buffer_t();
}

void vertex_buffer_delete(vertex_buffer_t* buffer)
{
    if (!buffer) {
        return;
    }

    delete buffer;
}

void vertex_buffer_clear(vertex_buffer_t* buffer)
{
    if (!buffer) {
        return;
    }
    buffer->vertex_data.clear();
    buffer->index_data.clear();
}

// --- Thread-Safe Global Font Storage ---
// The raw .ttf file data. Loaded once and shared by all threads.
std::vector<uint8_t> s_font_storage;
// Mutex to protect the one-time lazy loading of the font data.
static std::mutex s_font_storage_mutex;

struct thread_local_font_resources_t
{
    vertex_buffer_t* m_buffer = nullptr;
    int m_pixel_height = 0;
    std::uint64_t m_cache_epoch = 0;
    msdf_atlas_t m_atlas;

    ~thread_local_font_resources_t() = default;

    void destroy_resources()
    {
        if (m_buffer) {
            vertex_buffer_delete(m_buffer);
            m_buffer = nullptr;
        }
    }
};

thread_local_font_resources_t& thread_local_resources()
{
    return thread_local_singleton<thread_local_font_resources_t>();
}

std::atomic<std::uint64_t> s_next_cache_epoch{1};

struct cached_font_data_t
{
    msdf_atlas_t atlas;
    std::uint64_t cache_epoch = 0;
    Sha256::Digest font_digest{};
};

static std::mutex s_cached_fonts_mutex;
static std::unordered_map<int, std::shared_ptr<cached_font_data_t>> s_cached_fonts;

std::shared_ptr<cached_font_data_t> get_cached_font(
    int pixel_height,
    const Sha256::Digest& font_digest)
{
    std::lock_guard<std::mutex> lock(s_cached_fonts_mutex);
    auto it = s_cached_fonts.find(pixel_height);
    if (it != s_cached_fonts.end()) {
        if (it->second && it->second->font_digest == font_digest) {
            return it->second;
        }
    }
    return nullptr;
}

void store_cached_font(const std::shared_ptr<cached_font_data_t>& font)
{
    if (!font) {
        return;
    }
    std::lock_guard<std::mutex> lock(s_cached_fonts_mutex);

    constexpr std::size_t k_max_cached_fonts = 64;
    if (s_cached_fonts.size() >= k_max_cached_fonts) {
        auto it = s_cached_fonts.begin();
        if (it != s_cached_fonts.end()) {
            s_cached_fonts.erase(it);
        }
    }

    s_cached_fonts[font->atlas.pixel_height] = font;
}

vnm::msdf_text::options_t atlas_options()
{
    vnm::msdf_text::options_t options;
    options.atlas_size = k_atlas_texture_size;
    options.min_atlas_font_size = k_min_atlas_font_size;
    options.atlas_px_range = k_atlas_px_range;
    options.sharpness_bias = k_sharpness_bias;
    options.build_kerning_table = true;
    return options;
}

const std::vector<char32_t>& glyph_codepoints()
{
    static const std::vector<char32_t> codepoints = vnm::msdf_text::default_codepoints();
    return codepoints;
}

std::string glyph_seed_string()
{
    return vnm::msdf_text::codepoints_to_utf8(glyph_codepoints());
}

bool uv_in_range(float value)
{
    constexpr float k_uv_slop = 0.001f;
    return std::isfinite(value) && value >= -k_uv_slop && value <= 1.0f + k_uv_slop;
}

bool validate_cached_glyph(const msdf_glyph_t& g)
{
    return
        std::isfinite(g.advance_x) &&
        std::isfinite(g.plane_left) &&
        std::isfinite(g.plane_bottom) &&
        std::isfinite(g.plane_right) &&
        std::isfinite(g.plane_top) &&
        g.plane_right >= g.plane_left &&
        g.plane_top >= g.plane_bottom &&
        uv_in_range(g.uv_left) &&
        uv_in_range(g.uv_bottom) &&
        uv_in_range(g.uv_right) &&
        uv_in_range(g.uv_top) &&
        g.uv_right >= g.uv_left &&
        g.uv_bottom >= g.uv_top;
}

void add_text_to_vectors(
    const char* text,
    float x,
    float y,
    const msdf_atlas_t& atlas,
    std::vector<float>& vertex_data,
    std::vector<std::uint32_t>& index_data)
{
    if (!text) {
        return;
    }

    std::vector<text_vertex_t> vertices;
    std::vector<std::uint32_t> indices;
    vnm::msdf_text::append_text_quads(atlas, text, x, y, vertices, &indices);
    if (vertices.empty() || indices.empty()) {
        return;
    }

    if (vertex_data.size() % 8u != 0u) {
        return;
    }
    quint32 base_vertex = 0;
    if (!detail::to_qrhi_count(vertex_data.size() / 8u, base_vertex)) {
        return;
    }

    std::size_t added_float_count = 0;
    std::size_t new_float_count = 0;
    std::size_t new_index_count = 0;
    std::size_t new_vertex_count = 0;
    quint32 checked_qrhi_value = 0;
    if (!detail::checked_size_product(vertices.size(), 8u, added_float_count) ||
        !detail::checked_size_add(vertex_data.size(), added_float_count, new_float_count) ||
        !detail::checked_size_add(index_data.size(), indices.size(), new_index_count) ||
        !detail::checked_size_add(vertex_data.size() / 8u, vertices.size(), new_vertex_count) ||
        !detail::to_qrhi_count(new_vertex_count, checked_qrhi_value) ||
        !detail::qrhi_byte_size(new_float_count, sizeof(float), checked_qrhi_value) ||
        !detail::qrhi_byte_size(
            new_index_count, sizeof(std::uint32_t), checked_qrhi_value))
    {
        return;
    }
    index_data.reserve(new_index_count);
    for (std::uint32_t index : indices) {
        index_data.push_back(base_vertex + index);
    }

    const auto* first = reinterpret_cast<const float*>(vertices.data());
    vertex_data.insert(vertex_data.end(), first, first + vertices.size() * 8);
}

void add_text_to_buffer(const char* text, float x, float y, thread_local_font_resources_t* res)
{
    if (!res || !res->m_buffer) {
        return;
    }
    add_text_to_vectors(
        text,
        x,
        y,
        res->m_atlas,
        res->m_buffer->vertex_data,
        res->m_buffer->index_data);
}

Sha256::Digest compute_font_digest()
{
    Sha256 ctx;
    ctx.update(&k_cache_version, sizeof(k_cache_version));
    ctx.update(&k_min_atlas_font_size, sizeof(k_min_atlas_font_size));
    ctx.update(&k_atlas_px_range, sizeof(k_atlas_px_range));
    ctx.update(&k_sharpness_bias, sizeof(k_sharpness_bias));
    ctx.update(&k_atlas_texture_size, sizeof(k_atlas_texture_size));
    ctx.update(glyph_seed_string());
    ctx.update(s_font_storage);
    return ctx.finalize();
}

std::filesystem::path cache_file_path(int pixel_height, const Sha256::Digest& font_digest)
{
    static std::filesystem::path s_cache_dir;

    if (s_cache_dir.empty()) {
        // Prefer cache directory for disposable MSDF artifacts
        s_cache_dir = get_cache_directory();
        if (s_cache_dir.empty()) {
            // Fallback to data directory
            s_cache_dir = get_data_directory();
        }
        if (s_cache_dir.empty()) {
            // Last resort: current directory
            s_cache_dir = std::filesystem::current_path() / ".vnm_plot_cache";
            std::error_code ec;
            std::filesystem::create_directories(s_cache_dir, ec);
        }
    }

    std::ostringstream oss;
    oss << "msdf_cache_v" << k_cache_version << "_px" << pixel_height << "_font";
    oss << Sha256::to_hex(font_digest);
    oss << ".bin";
    return s_cache_dir / oss.str();
}

// Forward declarations for disk cache helpers
std::shared_ptr<cached_font_data_t> load_cached_font_from_disk(
    const std::filesystem::path& path,
    const Sha256::Digest& expected_digest,
    int pixel_height);

void save_cached_font_to_disk(
    const std::filesystem::path& path,
    const cached_font_data_t& font);

std::shared_ptr<cached_font_data_t> load_cached_font_from_disk(
    const std::filesystem::path& path,
    const Sha256::Digest& expected_digest,
    int pixel_height)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return nullptr;
    }

    auto read = [&](auto& val) -> bool {
        in.read(reinterpret_cast<char*>(&val), sizeof(val));
        return bool(in);
    };

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t height = 0;
    if (!read(magic) || !read(version) || !read(height)) {
        return nullptr;
    }
    constexpr std::uint32_t k_magic = 0x4d534446; // 'MSDF'
    if (magic != k_magic || version != k_cache_version || height != static_cast<std::uint32_t>(pixel_height)) {
        return nullptr;
    }

    Sha256::Digest digest{};
    in.read(reinterpret_cast<char*>(digest.data()), digest.size());
    if (!in || digest != expected_digest) {
        return nullptr;
    }

    auto font = std::make_shared<cached_font_data_t>();
    font->atlas.pixel_height = pixel_height;
    font->font_digest = digest;

    std::uint32_t atlas_size = 0;
    if (!read(atlas_size)) {
        return nullptr;
    }
    if (atlas_size != static_cast<std::uint32_t>(k_atlas_texture_size)) {
        return nullptr;
    }
    font->atlas.atlas_size = static_cast<int>(atlas_size);

    if (!read(font->atlas.px_range) ||
        !read(font->atlas.baseline_offset_px) ||
        !read(font->atlas.monospace_advance_px))
    {
        return nullptr;
    }
    if (!std::isfinite(font->atlas.px_range) ||
        !std::isfinite(font->atlas.baseline_offset_px) ||
        !std::isfinite(font->atlas.monospace_advance_px))
    {
        return nullptr;
    }
    std::uint8_t mono_reliable = 0;
    std::uint8_t padding[3]{};
    if (!read(mono_reliable) || !in.read(reinterpret_cast<char*>(padding), sizeof(padding))) {
        return nullptr;
    }
    font->atlas.monospace_advance_reliable = (mono_reliable != 0);

    std::uint32_t glyph_count = 0;
    if (!read(glyph_count)) {
        return nullptr;
    }
    const std::size_t glyph_count_limit =
        std::max<std::size_t>(glyph_codepoints().size() + 16u, 256u);
    if (glyph_count > glyph_count_limit) {
        return nullptr;
    }
    for (std::uint32_t i = 0; i < glyph_count; ++i) {
        std::uint32_t code = 0;
        msdf_glyph_t g{};
        if (!read(code) ||
            !read(g.advance_x) ||
            !read(g.plane_left) ||
            !read(g.plane_bottom) ||
            !read(g.plane_right) ||
            !read(g.plane_top) ||
            !read(g.uv_left) ||
            !read(g.uv_bottom) ||
            !read(g.uv_right) ||
            !read(g.uv_top))
        {
            return nullptr;
        }
        if (!validate_cached_glyph(g)) {
            return nullptr;
        }
        font->atlas.glyphs.emplace(static_cast<char32_t>(code), g);
    }

    std::uint32_t kerning_count = 0;
    if (!read(kerning_count)) {
        return nullptr;
    }
    const std::size_t kerning_limit =
        static_cast<std::size_t>(glyph_count) * static_cast<std::size_t>(glyph_count);
    if (kerning_count > kerning_limit) {
        return nullptr;
    }
    for (std::uint32_t i = 0; i < kerning_count; ++i) {
        msdf_kerning_key_t key{};
        float value = 0.f;
        if (!read(key.left) || !read(key.right) || !read(value)) {
            return nullptr;
        }
        if (!std::isfinite(value)) {
            return nullptr;
        }
        font->atlas.kerning_px.emplace(key, value);
    }

    std::uint32_t atlas_bytes = 0;
    if (!read(atlas_bytes)) {
        return nullptr;
    }
    const std::uint32_t expected_atlas_bytes =
        static_cast<std::uint32_t>(k_atlas_texture_size) *
        static_cast<std::uint32_t>(k_atlas_texture_size) *
        4u;
    if (atlas_bytes != expected_atlas_bytes) {
        return nullptr;
    }
    font->atlas.rgba.resize(atlas_bytes);
    if (!font->atlas.rgba.empty()) {
        in.read(reinterpret_cast<char*>(font->atlas.rgba.data()), atlas_bytes);
        if (!in) {
            return nullptr;
        }
    }

    font->cache_epoch = s_next_cache_epoch.fetch_add(1, std::memory_order_relaxed);
    return font;
}

void save_cached_font_to_disk(
    const std::filesystem::path& path,
    const cached_font_data_t& font)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }

    auto write = [&](auto val) {
        out.write(reinterpret_cast<const char*>(&val), sizeof(val));
    };

    constexpr std::uint32_t k_magic = 0x4d534446; // 'MSDF'
    write(k_magic);
    write(k_cache_version);
    write(static_cast<std::uint32_t>(font.atlas.pixel_height));
    out.write(reinterpret_cast<const char*>(font.font_digest.data()), font.font_digest.size());
    write(static_cast<std::uint32_t>(font.atlas.atlas_size));
    write(font.atlas.px_range);
    write(font.atlas.baseline_offset_px);
    write(font.atlas.monospace_advance_px);
    std::uint8_t mono_reliable = font.atlas.monospace_advance_reliable ? 1u : 0u;
    out.write(reinterpret_cast<const char*>(&mono_reliable), sizeof(mono_reliable));
    std::uint8_t padding[3]{0, 0, 0};
    out.write(reinterpret_cast<const char*>(padding), sizeof(padding));

    write(static_cast<std::uint32_t>(font.atlas.glyphs.size()));
    for (const auto& [code, g] : font.atlas.glyphs) {
        write(static_cast<std::uint32_t>(code));
        write(g.advance_x);
        write(g.plane_left);
        write(g.plane_bottom);
        write(g.plane_right);
        write(g.plane_top);
        write(g.uv_left);
        write(g.uv_bottom);
        write(g.uv_right);
        write(g.uv_top);
    }

    write(static_cast<std::uint32_t>(font.atlas.kerning_px.size()));
    for (const auto& [key, value] : font.atlas.kerning_px) {
        write(key.left);
        write(key.right);
        write(value);
    }

    write(static_cast<std::uint32_t>(font.atlas.rgba.size()));
    if (!font.atlas.rgba.empty()) {
        out.write(
            reinterpret_cast<const char*>(font.atlas.rgba.data()),
            static_cast<std::streamsize>(font.atlas.rgba.size()));
    }
}

std::shared_ptr<cached_font_data_t> build_font_cache(
    int pixel_height,
    const Sha256::Digest& font_digest,
    const std::function<void(const std::string&)>& log_error,
    const std::function<void(const std::string&)>& log_debug)
{
    auto font = std::make_shared<cached_font_data_t>();
    font->font_digest = font_digest;

    auto result = vnm::msdf_text::build_font_atlas(
        s_font_storage.data(),
        s_font_storage.size(),
        pixel_height,
        glyph_codepoints(),
        atlas_options(),
        log_debug);
    if (result.status == vnm::msdf_text::Build_status::FAILURE) {
        if (log_error) {
            log_error(result.message);
        }
        return nullptr;
    }

    font->atlas = std::move(result.atlas);
    font->cache_epoch = s_next_cache_epoch.fetch_add(1, std::memory_order_relaxed);

    return font;
}

std::shared_ptr<cached_font_data_t> load_or_build_font_cache(
    Asset_loader& asset_loader,
    int pixel_height,
    const std::function<void(const std::string&)>& log_error,
    const std::function<void(const std::string&)>& log_debug)
{
    if (s_font_storage.empty()) {
        std::lock_guard<std::mutex> locker(s_font_storage_mutex);
        if (s_font_storage.empty()) {
            auto font_data = asset_loader.load("fonts/monospace.ttf");
            if (font_data) {
                s_font_storage.assign(font_data->begin(), font_data->end());
            }
        }
    }

    if (s_font_storage.empty()) {
        if (log_error) {
            log_error("Failed to load MSDF font asset fonts/monospace.ttf");
        }
        return nullptr;
    }

    const auto font_digest = compute_font_digest();
    const bool disk_cache = s_disk_cache_enabled.load(std::memory_order_relaxed);

    auto cached = get_cached_font(pixel_height, font_digest);
    if (!cached && disk_cache) {
        const auto cache_path = cache_file_path(pixel_height, font_digest);
        cached = load_cached_font_from_disk(cache_path, font_digest, pixel_height);
        if (cached) {
            store_cached_font(cached);
        }
    }
    if (!cached) {
        cached = build_font_cache(pixel_height, font_digest, log_error, log_debug);
        if (cached) {
            store_cached_font(cached);
            if (disk_cache) {
                const auto cache_path = cache_file_path(pixel_height, font_digest);
                save_cached_font_to_disk(cache_path, *cached);
            }
        }
    }

    return cached;
}

} // anonymous namespace

namespace {

using detail::load_qsb;

struct Text_block_std140
{
    float pmv[16] = {};
    float color[4] = {};
    float shadow_color[4] = {};
    float px_range = 0.f;
    float shadow_radius = 0.f;
    float padding[2] = {};
};

static_assert(offsetof(Text_block_std140, pmv)              ==  0, "Text UBO pmv offset");
static_assert(offsetof(Text_block_std140, color)            == 64, "Text UBO color offset");
static_assert(offsetof(Text_block_std140, shadow_color)     == 80, "Text UBO shadow color offset");
static_assert(offsetof(Text_block_std140, px_range)         == 96, "Text UBO px_range offset");
static_assert(offsetof(Text_block_std140, shadow_radius)    == 100, "Text UBO shadow radius offset");
static_assert(sizeof(Text_block_std140)                     == 112, "Text UBO std140 size");

constexpr std::uint32_t k_text_ubo_bytes = sizeof(Text_block_std140);

struct rhi_text_call_t
{
    std::unique_ptr<QRhiBuffer> ubo;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    QRhiBuffer*      srb_last_ubo     = nullptr;
    QRhiTexture*     srb_last_texture = nullptr;
    QRhiSampler*     srb_last_sampler = nullptr;
};

enum class rhi_text_pass_t : std::uint8_t
{
    SHADOW,
    FOREGROUND,
};

struct rhi_text_draw_op_t
{
    std::size_t call_index  = 0;
    quint32     index_start = 0;
    quint32     index_count = 0;
    text_scissor_t scissor;
    rhi_text_pass_t pass = rhi_text_pass_t::FOREGROUND;
};

struct rhi_text_state_t
{
    QRhi* last_rhi = nullptr;

    std::unique_ptr<QRhiTexture> atlas_texture;
    std::unique_ptr<QRhiSampler> sampler;
    int atlas_size = 0;
    std::uint64_t uploaded_cache_epoch = 0;

    std::unique_ptr<QRhiBuffer> vbo;
    std::unique_ptr<QRhiBuffer> ibo;
    std::size_t vbo_capacity_bytes = 0;
    std::size_t ibo_capacity_bytes = 0;

    std::vector<rhi_text_call_t> calls;
    std::vector<rhi_text_draw_op_t> ops;
    std::size_t call_used = 0;

    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    QRhiRenderPassDescriptor* pipeline_rpd = nullptr;
    int pipeline_samples = 0;

    QShader vert;
    QShader frag;
    bool shaders_loaded = false;
};

} // anonymous namespace

// --- PIMPL Definition ---
struct Font_renderer::impl_t
{
    thread_local_font_resources_t* m_resources = nullptr;
    std::shared_ptr<cached_font_data_t> m_font_cache;
    int m_metric_pixel_height = 0;
    std::function<void(const std::string&)> m_log_error;
    std::function<void(const std::string&)> m_log_debug;
    bool m_rhi_batch_active = false;
    std::vector<float> m_rhi_vertex_data;
    std::vector<std::uint32_t> m_rhi_index_data;
    std::vector<float> m_rhi_frame_vertex_data;
    std::vector<std::uint32_t> m_rhi_frame_index_data;

    rhi_text_state_t m_rhi;

    // m_resources is the live thread-local atlas the renderer mutates and
    // m_font_cache is the cross-thread snapshot used by readers. The
    // metric/measure accessors below prefer m_resources when set and fall
    // back to m_font_cache; this helper centralizes the choice so each
    // accessor doesn't repeat the "res ? res->X : (cached ? cached->Y : ...)"
    // dance.
    const msdf_atlas_t* current_atlas() const
    {
        if (m_resources) {
            return &m_resources->m_atlas;
        }
        if (m_font_cache) {
            return &m_font_cache->atlas;
        }
        return nullptr;
    }

    std::uint64_t current_cache_epoch() const
    {
        if (m_resources) {
            return m_resources->m_cache_epoch;
        }
        return m_font_cache ? m_font_cache->cache_epoch : std::uint64_t{0};
    }
};

// --- Public API Implementation ---

Font_renderer::Font_renderer()
    : m_impl(std::make_unique<impl_t>())
{
}

Font_renderer::~Font_renderer() = default;

void Font_renderer::set_log_callbacks(
    std::function<void(const std::string&)> log_error,
    std::function<void(const std::string&)> log_debug)
{
    m_impl->m_log_error = log_error;
    m_impl->m_log_debug = log_debug;
}

void Font_renderer::initialize(Asset_loader& asset_loader, int pixel_height, bool force_rebuild)
{
    initialize_metrics(asset_loader, pixel_height, force_rebuild);
    auto& resources = thread_local_resources();
    if (force_rebuild || resources.m_pixel_height != pixel_height) {
        resources.destroy_resources();
        resources.m_buffer = vertex_buffer_new("vertex:2f,tex_coord:2f,tex_bounds:4f");
    }
    if (!m_impl->m_font_cache) {
        m_impl->m_resources = nullptr;
        return;
    }

    const auto& cached = *m_impl->m_font_cache;
    resources.m_pixel_height = pixel_height;
    resources.m_cache_epoch = cached.cache_epoch;
    resources.m_atlas = cached.atlas;
    m_impl->m_resources = &resources;
}

void Font_renderer::initialize_metrics(Asset_loader& asset_loader, int pixel_height, bool force_rebuild)
{
    if (!force_rebuild &&
        m_impl->m_font_cache &&
        m_impl->m_metric_pixel_height == pixel_height)
    {
        return;
    }

    auto cached = load_or_build_font_cache(
        asset_loader,
        pixel_height,
        m_impl->m_log_error,
        m_impl->m_log_debug);
    if (!cached) {
        return;
    }

    m_impl->m_font_cache = std::move(cached);
    m_impl->m_metric_pixel_height = pixel_height;
}

void Font_renderer::deinitialize()
{
    // Detach this instance from the thread-local GPU resources.
    // We do not force global TLS teardown here; resources are reclaimed on normal process exit.
    m_impl->m_resources = nullptr;
}

float Font_renderer::measure_text_px(const char* text) const
{
    const msdf_atlas_t* atlas = m_impl->current_atlas();
    if (!text || !atlas) {
        return 0.0f;
    }
    return vnm::msdf_text::measure_text_px(*atlas, text);
}

std::uint64_t Font_renderer::text_measure_cache_key() const
{
    return m_impl->current_cache_epoch();
}

float Font_renderer::monospace_advance_px() const
{
    const msdf_atlas_t* atlas = m_impl->current_atlas();
    return atlas ? atlas->monospace_advance_px : 0.f;
}

bool Font_renderer::monospace_advance_is_reliable() const
{
    const msdf_atlas_t* atlas = m_impl->current_atlas();
    return atlas ? atlas->monospace_advance_reliable : false;
}

float Font_renderer::compute_numeric_bottom() const
{
    const msdf_atlas_t* atlas = m_impl->current_atlas();
    if (!atlas) {
        return 0.0f;
    }
    static const char* k_sample = "0123456789-+.,";
    float max_bottom = -std::numeric_limits<float>::infinity();
    for (const char* p = k_sample; *p; ++p) {
        const auto it = atlas->glyphs.find(static_cast<unsigned char>(*p));
        if (it != atlas->glyphs.end()) {
            const float neg_bottom = -(it->second.plane_bottom);
            if (neg_bottom > max_bottom) {
                max_bottom = neg_bottom;
            }
        }
    }
    return std::isfinite(max_bottom) ? max_bottom : 0.0f;
}

float Font_renderer::baseline_offset_px() const
{
    const msdf_atlas_t* atlas = m_impl->current_atlas();
    return atlas ? atlas->baseline_offset_px : 0.f;
}

void Font_renderer::batch_text(float x, float y, const char* text)
{
    if (m_impl->m_rhi_batch_active) {
        const auto* cached = m_impl->m_font_cache.get();
        if (!cached) {
            return;
        }
        add_text_to_vectors(
            text,
            x,
            y,
            cached->atlas,
            m_impl->m_rhi_vertex_data,
            m_impl->m_rhi_index_data);
        return;
    }

    auto* res = m_impl->m_resources;
    if (!res) {
        return;
    }
    add_text_to_buffer(text, x, y, res);
}

void Font_renderer::rhi_begin_frame()
{
    m_impl->m_rhi_batch_active = true;
    m_impl->m_rhi_vertex_data.clear();
    m_impl->m_rhi_index_data.clear();
    m_impl->m_rhi_frame_vertex_data.clear();
    m_impl->m_rhi_frame_index_data.clear();
    m_impl->m_rhi.ops.clear();
    m_impl->m_rhi.call_used = 0;
}

void Font_renderer::rhi_queue_draw(
    const frame_context_t& ctx,
    const glm::mat4& pmv,
    const glm::vec4& color,
    const text_scissor_t& scissor,
    const text_shadow_t& shadow)
{
    if (!ctx.rhi || !ctx.rhi_updates || !ctx.render_target || !m_impl->m_font_cache) {
        m_impl->m_rhi_vertex_data.clear();
        m_impl->m_rhi_index_data.clear();
        return;
    }
    if (m_impl->m_rhi_index_data.empty() || m_impl->m_rhi_vertex_data.empty()) {
        return;
    }

    const std::size_t vertex_start_float_count =
        m_impl->m_rhi_frame_vertex_data.size();
    if (vertex_start_float_count % 8u != 0u ||
        m_impl->m_rhi_vertex_data.size() % 8u != 0u)
    {
        m_impl->m_rhi_vertex_data.clear();
        m_impl->m_rhi_index_data.clear();
        return;
    }

    quint32 index_start = 0;
    quint32 base_vertex = 0;
    quint32 index_count = 0;
    if (!detail::to_qrhi_count(
            m_impl->m_rhi_frame_index_data.size(), index_start) ||
        !detail::to_qrhi_count(vertex_start_float_count / 8u, base_vertex) ||
        !detail::to_qrhi_count(m_impl->m_rhi_index_data.size(), index_count))
    {
        m_impl->m_rhi_vertex_data.clear();
        m_impl->m_rhi_index_data.clear();
        return;
    }

    std::size_t new_vertex_float_count = 0;
    std::size_t new_index_count = 0;
    std::size_t queued_vertex_count = 0;
    quint32 checked_qrhi_bytes = 0;
    if (!detail::checked_size_add(
            m_impl->m_rhi_frame_vertex_data.size(),
            m_impl->m_rhi_vertex_data.size(),
            new_vertex_float_count) ||
        !detail::checked_size_add(
            m_impl->m_rhi_frame_index_data.size(),
            m_impl->m_rhi_index_data.size(),
            new_index_count) ||
        !detail::checked_size_add(
            vertex_start_float_count / 8u,
            m_impl->m_rhi_vertex_data.size() / 8u,
            queued_vertex_count) ||
        !detail::to_qrhi_count(queued_vertex_count, checked_qrhi_bytes) ||
        !detail::qrhi_byte_size(
            new_vertex_float_count, sizeof(float), checked_qrhi_bytes) ||
        !detail::qrhi_byte_size(
            new_index_count, sizeof(std::uint32_t), checked_qrhi_bytes))
    {
        m_impl->m_rhi_vertex_data.clear();
        m_impl->m_rhi_index_data.clear();
        return;
    }

    m_impl->m_rhi_frame_vertex_data.insert(
        m_impl->m_rhi_frame_vertex_data.end(),
        m_impl->m_rhi_vertex_data.begin(),
        m_impl->m_rhi_vertex_data.end());
    m_impl->m_rhi_frame_index_data.reserve(new_index_count);
    for (std::uint32_t index : m_impl->m_rhi_index_data) {
        m_impl->m_rhi_frame_index_data.push_back(index + base_vertex);
    }

    auto& rhi_state = m_impl->m_rhi;
    QRhi* rhi = ctx.rhi;
    QRhiResourceUpdateBatch* updates = ctx.rhi_updates;

    if (rhi_state.last_rhi != rhi) {
        rhi_state = rhi_text_state_t{};
        rhi_state.last_rhi = rhi;
    }

    if (!rhi_state.shaders_loaded) {
        rhi_state.vert = load_qsb("msdf_text.vert.qsb");
        rhi_state.frag = load_qsb("msdf_text.frag.qsb");
        rhi_state.shaders_loaded = true;
    }

    const auto& cached = *m_impl->m_font_cache;
    if (!rhi_state.atlas_texture ||
        rhi_state.atlas_size != cached.atlas.atlas_size ||
        rhi_state.uploaded_cache_epoch != cached.cache_epoch)
    {
        rhi_state.atlas_texture.reset(rhi->newTexture(
            QRhiTexture::RGBA8,
            QSize(cached.atlas.atlas_size, cached.atlas.atlas_size)));
        if (!rhi_state.atlas_texture || !rhi_state.atlas_texture->create()) {
            rhi_state.atlas_texture.reset();
            m_impl->m_rhi_vertex_data.clear();
            m_impl->m_rhi_index_data.clear();
            return;
        }
        QImage image(
            cached.atlas.rgba.data(),
            cached.atlas.atlas_size,
            cached.atlas.atlas_size,
            cached.atlas.atlas_size * 4,
            QImage::Format_RGBA8888);
        updates->uploadTexture(rhi_state.atlas_texture.get(), image);
        rhi_state.atlas_size = cached.atlas.atlas_size;
        rhi_state.uploaded_cache_epoch = cached.cache_epoch;
        for (auto& call : rhi_state.calls) {
            call.srb.reset();
            call.srb_last_texture = nullptr;
        }
    }

    if (!rhi_state.sampler) {
        rhi_state.sampler.reset(rhi->newSampler(
            QRhiSampler::Linear,
            QRhiSampler::Linear,
            QRhiSampler::None,
            QRhiSampler::ClampToEdge,
            QRhiSampler::ClampToEdge));
        if (!rhi_state.sampler || !rhi_state.sampler->create()) {
            rhi_state.sampler.reset();
            m_impl->m_rhi_vertex_data.clear();
            m_impl->m_rhi_index_data.clear();
            return;
        }
    }

    const auto acquire_call = [&]() -> std::size_t {
        if (rhi_state.call_used == rhi_state.calls.size()) {
            rhi_state.calls.emplace_back();
        }
        const std::size_t call_index = rhi_state.call_used++;
        auto& call = rhi_state.calls[call_index];

        if (!call.ubo) {
            call.ubo.reset(rhi->newBuffer(
                QRhiBuffer::Dynamic,
                QRhiBuffer::UniformBuffer,
                k_text_ubo_bytes));
            if (!call.ubo || !call.ubo->create()) {
                call.ubo.reset();
                --rhi_state.call_used;
                return std::numeric_limits<std::size_t>::max();
            }
        }

        if (!call.srb ||
            call.srb_last_ubo != call.ubo.get() ||
            call.srb_last_texture != rhi_state.atlas_texture.get() ||
            call.srb_last_sampler != rhi_state.sampler.get())
        {
            call.srb.reset(rhi->newShaderResourceBindings());
            call.srb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0,
                    QRhiShaderResourceBinding::VertexStage
                        | QRhiShaderResourceBinding::FragmentStage,
                    call.ubo.get(),
                    0,
                    k_text_ubo_bytes),
                QRhiShaderResourceBinding::sampledTexture(
                    1,
                    QRhiShaderResourceBinding::FragmentStage,
                    rhi_state.atlas_texture.get(),
                    rhi_state.sampler.get())
            });
            if (!call.srb->create()) {
                call.srb.reset();
                --rhi_state.call_used;
                return std::numeric_limits<std::size_t>::max();
            }
            call.srb_last_ubo     = call.ubo.get();
            call.srb_last_texture = rhi_state.atlas_texture.get();
            call.srb_last_sampler = rhi_state.sampler.get();
        }

        return call_index;
    };

    const bool has_shadow = shadow.radius_px > 0.0f && shadow.color.a > 0.0f;
    const std::size_t first_call_index = acquire_call();
    if (first_call_index == std::numeric_limits<std::size_t>::max()) {
        m_impl->m_rhi_vertex_data.clear();
        m_impl->m_rhi_index_data.clear();
        return;
    }

    std::size_t shadow_call_index = std::numeric_limits<std::size_t>::max();
    std::size_t foreground_call_index = first_call_index;
    if (has_shadow) {
        shadow_call_index = first_call_index;
        foreground_call_index = acquire_call();
        if (foreground_call_index == std::numeric_limits<std::size_t>::max()) {
            m_impl->m_rhi_vertex_data.clear();
            m_impl->m_rhi_index_data.clear();
            return;
        }
    }

    auto& first_call = rhi_state.calls[first_call_index];

    QRhiRenderPassDescriptor* current_rpd = ctx.render_target->renderPassDescriptor();
    const int current_samples = ctx.render_target->sampleCount();
    if (rhi_state.pipeline &&
        (rhi_state.pipeline_rpd != current_rpd ||
         rhi_state.pipeline_samples != current_samples))
    {
        rhi_state.pipeline.reset();
    }

    if (!rhi_state.pipeline) {
        std::unique_ptr<QRhiShaderResourceBindings> layout_srb(
            rhi->newShaderResourceBindings());
        layout_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                first_call.ubo.get(),
                0,
                k_text_ubo_bytes),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                rhi_state.atlas_texture.get(),
                rhi_state.sampler.get())
        });
        if (!layout_srb->create()) {
            m_impl->m_rhi_vertex_data.clear();
            m_impl->m_rhi_index_data.clear();
            return;
        }

        QRhiVertexInputLayout vlayout;
        QRhiVertexInputBinding binding(
            static_cast<quint32>(sizeof(text_vertex_t)));
        QRhiVertexInputAttribute position(
            0, 0, QRhiVertexInputAttribute::Float2,
            static_cast<quint32>(offsetof(text_vertex_t, x)));
        QRhiVertexInputAttribute tex_coord(
            0, 1, QRhiVertexInputAttribute::Float2,
            static_cast<quint32>(offsetof(text_vertex_t, s)));
        QRhiVertexInputAttribute tex_bounds(
            0, 2, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(text_vertex_t, s_min)));
        vlayout.setBindings({binding});
        vlayout.setAttributes({position, tex_coord, tex_bounds});

        rhi_state.pipeline.reset(rhi->newGraphicsPipeline());
        rhi_state.pipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   rhi_state.vert },
            { QRhiShaderStage::Fragment, rhi_state.frag }
        });
        rhi_state.pipeline->setVertexInputLayout(vlayout);
        rhi_state.pipeline->setShaderResourceBindings(layout_srb.get());
        rhi_state.pipeline->setTopology(QRhiGraphicsPipeline::Triangles);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        rhi_state.pipeline->setTargetBlends({blend});
        rhi_state.pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
        rhi_state.pipeline->setRenderPassDescriptor(current_rpd);
        rhi_state.pipeline->setSampleCount(current_samples);

        if (!rhi_state.pipeline->create()) {
            rhi_state.pipeline.reset();
            m_impl->m_rhi_vertex_data.clear();
            m_impl->m_rhi_index_data.clear();
            return;
        }
        rhi_state.pipeline_rpd = current_rpd;
        rhi_state.pipeline_samples = current_samples;
    }

    const auto queue_text_pass = [&](std::size_t call_index,
                                     const glm::vec4& draw_color,
                                     const text_shadow_t& draw_shadow,
                                     rhi_text_pass_t pass) {
        auto& call = rhi_state.calls[call_index];

        Text_block_std140 block{};
        std::memcpy(block.pmv, glm::value_ptr(pmv), sizeof(block.pmv));
        block.color[0] = draw_color.r;
        block.color[1] = draw_color.g;
        block.color[2] = draw_color.b;
        block.color[3] = draw_color.a;
        block.shadow_color[0] = draw_shadow.color.r;
        block.shadow_color[1] = draw_shadow.color.g;
        block.shadow_color[2] = draw_shadow.color.b;
        block.shadow_color[3] = draw_shadow.color.a;
        block.px_range = cached.atlas.px_range;
        block.shadow_radius = draw_shadow.radius_px;
        updates->updateDynamicBuffer(call.ubo.get(), 0, sizeof(block), &block);

        rhi_text_draw_op_t op{};
        op.call_index  = call_index;
        op.index_start = index_start;
        op.index_count = index_count;
        op.scissor     = scissor;
        op.pass        = pass;
        rhi_state.ops.push_back(op);
    };

    if (has_shadow) {
        glm::vec4 transparent_text = color;
        transparent_text.a = 0.0f;
        queue_text_pass(
            shadow_call_index,
            transparent_text,
            shadow,
            rhi_text_pass_t::SHADOW);
    }

    queue_text_pass(
        foreground_call_index,
        color,
        text_shadow_t{},
        rhi_text_pass_t::FOREGROUND);

    m_impl->m_rhi_vertex_data.clear();
    m_impl->m_rhi_index_data.clear();
}

void Font_renderer::rhi_finalize_frame(const frame_context_t& ctx)
{
    auto& rhi_state = m_impl->m_rhi;
    if (!ctx.rhi || !ctx.rhi_updates ||
        m_impl->m_rhi_frame_vertex_data.empty() ||
        m_impl->m_rhi_frame_index_data.empty())
    {
        return;
    }

    QRhi* rhi = ctx.rhi;
    QRhiResourceUpdateBatch* updates = ctx.rhi_updates;

    std::size_t vertex_bytes = 0;
    std::size_t index_bytes = 0;
    quint32 qrhi_vertex_bytes = 0;
    quint32 qrhi_index_bytes = 0;
    if (!detail::qrhi_byte_size(
            m_impl->m_rhi_frame_vertex_data.size(), sizeof(float),
            vertex_bytes, qrhi_vertex_bytes) ||
        !detail::qrhi_byte_size(
            m_impl->m_rhi_frame_index_data.size(), sizeof(std::uint32_t),
            index_bytes, qrhi_index_bytes))
    {
        rhi_reset_frame();
        return;
    }

    if (!rhi_state.vbo || rhi_state.vbo_capacity_bytes < vertex_bytes) {
        std::size_t alloc = 0;
        quint32 qrhi_alloc = 0;
        if (!detail::qrhi_grown_capacity_bytes(vertex_bytes, alloc, qrhi_alloc)) {
            rhi_reset_frame();
            return;
        }
        rhi_state.vbo.reset(rhi->newBuffer(
            QRhiBuffer::Dynamic,
            QRhiBuffer::VertexBuffer,
            qrhi_alloc));
        if (!rhi_state.vbo || !rhi_state.vbo->create()) {
            rhi_state.vbo.reset();
            rhi_reset_frame();
            return;
        }
        rhi_state.vbo_capacity_bytes = alloc;
    }

    if (!rhi_state.ibo || rhi_state.ibo_capacity_bytes < index_bytes) {
        std::size_t alloc = 0;
        quint32 qrhi_alloc = 0;
        if (!detail::qrhi_grown_capacity_bytes(index_bytes, alloc, qrhi_alloc)) {
            rhi_reset_frame();
            return;
        }
        rhi_state.ibo.reset(rhi->newBuffer(
            QRhiBuffer::Dynamic,
            QRhiBuffer::IndexBuffer,
            qrhi_alloc));
        if (!rhi_state.ibo || !rhi_state.ibo->create()) {
            rhi_state.ibo.reset();
            rhi_reset_frame();
            return;
        }
        rhi_state.ibo_capacity_bytes = alloc;
    }

    updates->updateDynamicBuffer(
        rhi_state.vbo.get(),
        0,
        qrhi_vertex_bytes,
        m_impl->m_rhi_frame_vertex_data.data());
    updates->updateDynamicBuffer(
        rhi_state.ibo.get(),
        0,
        qrhi_index_bytes,
        m_impl->m_rhi_frame_index_data.data());
}

void Font_renderer::rhi_record_frame(const frame_context_t& ctx)
{
    auto& rhi_state = m_impl->m_rhi;
    if (!ctx.cb || !rhi_state.pipeline || !rhi_state.vbo || !rhi_state.ibo) {
        rhi_reset_frame();
        return;
    }

    QRhiCommandBuffer* cb = ctx.cb;
    cb->setGraphicsPipeline(rhi_state.pipeline.get());

    QRhiCommandBuffer::VertexInput vertex_input{rhi_state.vbo.get(), 0u};
    const auto record_pass = [&](rhi_text_pass_t pass) {
        for (const auto& op : rhi_state.ops) {
            if (op.pass != pass ||
                op.call_index >= rhi_state.calls.size() ||
                op.index_count == 0)
            {
                continue;
            }
            const auto& call = rhi_state.calls[op.call_index];
            if (!call.srb) {
                continue;
            }

            cb->setShaderResources(call.srb.get());
            cb->setVertexInput(
                0,
                1,
                &vertex_input,
                rhi_state.ibo.get(),
                0,
                QRhiCommandBuffer::IndexUInt32);
            if (op.scissor.enabled) {
                cb->setScissor(QRhiScissor(
                    op.scissor.x,
                    op.scissor.y,
                    op.scissor.width,
                    op.scissor.height));
            }
            else {
                cb->setScissor(QRhiScissor(0, 0, ctx.win_w, ctx.win_h));
            }
            cb->drawIndexed(op.index_count, 1, op.index_start, 0, 0);
        }
    };

    record_pass(rhi_text_pass_t::SHADOW);
    record_pass(rhi_text_pass_t::FOREGROUND);

    rhi_reset_frame();
}

void Font_renderer::rhi_reset_frame()
{
    m_impl->m_rhi_batch_active = false;
    m_impl->m_rhi_vertex_data.clear();
    m_impl->m_rhi_index_data.clear();
    m_impl->m_rhi_frame_vertex_data.clear();
    m_impl->m_rhi_frame_index_data.clear();
    m_impl->m_rhi.ops.clear();
    m_impl->m_rhi.call_used = 0;
}

void Font_renderer::clear_buffer()
{
    if (m_impl->m_rhi_batch_active) {
        m_impl->m_rhi_vertex_data.clear();
        m_impl->m_rhi_index_data.clear();
        return;
    }

    auto* res = m_impl->m_resources;
    if (res && res->m_buffer) {
        vertex_buffer_clear(res->m_buffer);
    }
}

} // namespace vnm::plot
