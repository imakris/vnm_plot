#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/asset_loader.h>
#include "platform_paths.h"
#include "sha256.h"
#include "utf8_utils.h"
#include "tls_registry.h"

#include <glm/gtc/type_ptr.hpp>
#include <msdfgen.h>
#include <msdfgen-ext.h>

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
constexpr float k_atlas_px_range = 2.0f;
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

struct text_vertex_t
{
    float x;
    float y;
    float s;
    float t;
    float s_min;
    float t_min;
    float s_max;
    float t_max;
};

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

std::size_t vertex_buffer_vertex_count(const vertex_buffer_t* buffer)
{
    return buffer ? buffer->vertex_data.size() / 8 : 0;
}

void vertex_buffer_push_back_vertices(vertex_buffer_t* buffer, const void* vertices, std::size_t count)
{
    if (!buffer || !vertices || count == 0) {
        return;
    }

    const float* data = static_cast<const float*>(vertices);
    buffer->vertex_data.insert(buffer->vertex_data.end(), data, data + (count * 8));
}

void vertex_buffer_push_back_indices(vertex_buffer_t* buffer, const std::uint32_t* indices, std::size_t count)
{
    if (!buffer || !indices || count == 0) {
        return;
    }

    buffer->index_data.insert(buffer->index_data.end(), indices, indices + count);
}

void vertex_buffer_render(vertex_buffer_t* buffer)
{
    if (!buffer || buffer->index_data.empty() || buffer->vertex_data.empty()) {
        return;
    }
    buffer->vertex_data.clear();
    buffer->index_data.clear();
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

struct msdf_glyph_t
{
    float advance_x = 0.f;
    float plane_left = 0.f;
    float plane_bottom = 0.f;
    float plane_right = 0.f;
    float plane_top = 0.f;
    float uv_left = 0.f;
    float uv_bottom = 0.f;
    float uv_right = 0.f;
    float uv_top = 0.f;
};

struct msdf_kerning_key_t
{
    char32_t left;
    char32_t right;
};

struct msdf_kerning_key_hash_t
{
    std::size_t operator()(const msdf_kerning_key_t& key) const
    {
        return (static_cast<std::size_t>(key.left) << 32) ^ static_cast<std::size_t>(key.right);
    }
};

struct msdf_kerning_key_eq_t
{
    bool operator()(const msdf_kerning_key_t& a, const msdf_kerning_key_t& b) const
    {
        return a.left == b.left && a.right == b.right;
    }
};

struct thread_local_font_resources_t
{
    vertex_buffer_t* m_buffer = nullptr;
    int m_pixel_height = 0;
    std::uint64_t m_cache_epoch = 0;
    float m_monospace_advance_px = 0.f;
    bool m_monospace_advance_reliable = false;
    float m_px_range = std::numeric_limits<float>::quiet_NaN();
    float m_baseline_offset_px = 0.f;

    std::unordered_map<char32_t, msdf_glyph_t> m_glyphs;
    std::unordered_map<msdf_kerning_key_t, float, msdf_kerning_key_hash_t, msdf_kerning_key_eq_t> m_kerning_px;

    ~thread_local_font_resources_t() = default;

    void destroy_resources()
    {
        if (m_buffer) {
            vertex_buffer_delete(m_buffer);
            m_buffer = nullptr;
        }
    }
};

Thread_local_registry<thread_local_font_resources_t>& font_registry()
{
    static Thread_local_registry<thread_local_font_resources_t> registry;
    return registry;
}

thread_local_font_resources_t& thread_local_resources()
{
    return font_registry().get_or_create([] {
        return std::make_unique<thread_local_font_resources_t>();
    });
}

std::atomic<std::uint64_t> s_next_cache_epoch{1};

struct cached_font_data_t
{
    int pixel_height = 0;
    int atlas_size = 0;
    std::vector<std::uint8_t> atlas_rgba;
    std::unordered_map<char32_t, msdf_glyph_t> glyphs;
    std::unordered_map<msdf_kerning_key_t, float, msdf_kerning_key_hash_t, msdf_kerning_key_eq_t> kerning_px;
    float px_range = 0.f;
    float baseline_offset_px = 0.f;
    float monospace_advance_px = 0.f;
    bool monospace_advance_reliable = false;
    std::uint64_t cache_epoch = 0;
    Sha256::Digest font_digest{};
};

static std::mutex s_cached_fonts_mutex;
static std::unordered_map<int, std::shared_ptr<cached_font_data_t>> s_cached_fonts;

std::shared_ptr<cached_font_data_t> get_cached_font(int pixel_height)
{
    std::lock_guard<std::mutex> lock(s_cached_fonts_mutex);
    auto it = s_cached_fonts.find(pixel_height);
    if (it != s_cached_fonts.end()) {
        return it->second;
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

    s_cached_fonts[font->pixel_height] = font;
}

const std::vector<char32_t>& glyph_codepoints()
{
    static const std::vector<char32_t> cp = [] {
        static const char* ascii_printable_characters =
            " 0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

        static const char* latin_accented_characters =
            "\xC3\x80\xC3\x81\xC3\x82\xC3\x83\xC3\x84\xC3\x85\xC3\x86\xC3\x87\xC3\x88\xC3\x89\xC3\x8A"
            "\xC3\x8B\xC3\x8C\xC3\x8D\xC3\x8E\xC3\x8F\xC3\x90\xC3\x91\xC3\x92\xC3\x93\xC3\x94\xC3\x95"
            "\xC3\x96\xC3\x98\xC3\x99\xC3\x9A\xC3\x9B\xC3\x9C\xC3\x9D\xC3\x9E\xC3\x9F\xC3\xA0\xC3\xA1"
            "\xC3\xA2\xC3\xA3\xC3\xA4\xC3\xA5\xC3\xA6\xC3\xA7\xC3\xA8\xC3\xA9\xC3\xAA\xC3\xAB\xC3\xAC"
            "\xC3\xAD\xC3\xAE\xC3\xAF\xC3\xB0\xC3\xB1\xC3\xB2\xC3\xB3\xC3\xB4\xC3\xB5\xC3\xB6\xC3\xB8"
            "\xC3\xB9\xC3\xBA\xC3\xBB\xC3\xBC\xC3\xBD\xC3\xBE\xC3\xBF\xC5\x92\xC5\x93\xC5\xA0\xC5\xA1"
            "\xC5\xB8\xC6\x92";

        static const char* greek_characters =
            "\xCE\x91\xCE\x92\xCE\x93\xCE\x94\xCE\x95\xCE\x96\xCE\x97\xCE\x98\xCE\x99\xCE\x9A\xCE\x9B"
            "\xCE\x9C\xCE\x9D\xCE\x9E\xCE\x9F\xCE\xA0\xCE\xA1\xCE\xA3\xCE\xA4\xCE\xA5\xCE\xA6\xCE\xA7"
            "\xCE\xA8\xCE\xA9\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4\xCE\xB5\xCE\xB6\xCE\xB7\xCE\xB8\xCE\xB9"
            "\xCE\xBA\xCE\xBB\xCE\xBC\xCE\xBD\xCE\xBE\xCE\xBF\xCF\x80\xCF\x81\xCF\x83\xCF\x84\xCF\x85"
            "\xCF\x86\xCF\x87\xCF\x88\xCF\x89\xCE\x86\xCE\x88\xCE\x89\xCE\x8A\xCE\x8C\xCE\x8E\xCE\x8F"
            "\xCE\xAC\xCE\xAD\xCE\xAE\xCE\xAF\xCF\x8C\xCF\x8D\xCF\x8E\xCF\x8A\xCF\x8B\xCE\x90\xCE\xB0"
            "\xCE\xAA\xCE\xAB\xCF\x82\xC2\xAB\xC2\xBB\xCE\x87";

        static const char* currency_popular_characters =
            "\xE2\x82\xAC\xC2\xA2\xC2\xA3\xC2\xA4\xC2\xA5\xE0\xB8\xBF\xE2\x82\xBD\xE2\x82\xB9\xE2"
            "\x82\xA9";

        static const char* currency_all_characters =
            "\xE2\x82\xB5\xD8\x8B\xE0\xA7\xB2\xE0\xA7\xB3\xE0\xA7\xBB\xE0\xAB\xB1\xE0\xAF\xB9\xE1\x9F"
            "\x9B\xE2\x82\xA0\xE2\x82\xA1\xE2\x82\xA2\xE2\x82\xA3\xE2\x82\xA4\xE2\x82\xA5\xE2\x82\xA6"
            "\xE2\x82\xA7\xE2\x82\xA8\xE2\x82\xAA\xE2\x82\xAB\xE2\x82\xAD\xE2\x82\xAE\xE2\x82\xAF\xE2"
            "\x82\xB0\xE2\x82\xB1\xE2\x82\xB2\xE2\x82\xB3\xE2\x82\xB4\xE2\x82\xB8\xE2\x82\xBA\xE2\x82"
            "\xBC\xE2\x82\xBE\xEF\xB7\xBC\xEF\xB9\xA9\xEF\xBC\x84\xEF\xBF\xA0\xEF\xBF\xA1\xEF\xBF\xA5"
            "\xEF\xBF\xA6";

        static const char* ui_symbol_characters =
            "\xE2\x98\x90"
            "\xE2\x98\x91"
            "\xE2\x98\x92"
            "\xF0\x9F\x94\x98"
            "\xF0\x9F\x97\x95"
            "\xF0\x9F\x97\x96"
            "\xF0\x9F\x97\x97"
            "\xE2\x9C\x95";

        std::vector<char32_t> chars;
        const auto append = [&chars](const char* utf8) {
            const auto decoded = utf8_to_codepoints(utf8);
            chars.insert(chars.end(), decoded.begin(), decoded.end());
        };
        append(ascii_printable_characters);
        append(latin_accented_characters);
        append(greek_characters);
        append(currency_popular_characters);
        append(currency_all_characters);
        append(ui_symbol_characters);
        std::sort(chars.begin(), chars.end());
        chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
        return chars;
    }();
    return cp;
}

std::string glyph_seed_string()
{
    // Derive seed string from glyph_codepoints() to ensure consistency.
    // The seed is used for font cache digest computation.
    //
    // Note: glyph_codepoints() returns sorted/unique codepoints, so the
    // resulting UTF-8 string differs from the original insertion order.
    // This changes the font cache digest, causing a one-time cache
    // regeneration on upgrade. This is acceptable as the cache is
    // automatically rebuilt when the digest mismatches.
    return codepoints_to_utf8(glyph_codepoints());
}

template <typename GlyphMap, typename KerningMap>
void add_text_to_vectors(
    const char* text,
    glm::vec2* pen,
    const GlyphMap& glyphs,
    const KerningMap& kerning_px,
    std::vector<float>& vertex_data,
    std::vector<std::uint32_t>& index_data)
{
    const auto codepoints = utf8_to_codepoints(text);
    char32_t previous = 0;
    for (const auto codepoint : codepoints) {
        const auto g_it = glyphs.find(codepoint);
        if (g_it == glyphs.end()) {
            continue;
        }

        if (previous != 0) {
            const msdf_kerning_key_t key{previous, codepoint};
            const auto k_it = kerning_px.find(key);
            if (k_it != kerning_px.end()) {
                pen->x += k_it->second;
            }
        }

        const msdf_glyph_t& glyph = g_it->second;
        const float x0 = pen->x + glyph.plane_left;
        const float x1 = pen->x + glyph.plane_right;
        const float y0 = pen->y + glyph.plane_bottom;
        const float y1 = pen->y + glyph.plane_top;
        const float s_min = std::min(glyph.uv_left, glyph.uv_right);
        const float s_max = std::max(glyph.uv_left, glyph.uv_right);
        const float t_min = std::min(glyph.uv_top, glyph.uv_bottom);
        const float t_max = std::max(glyph.uv_top, glyph.uv_bottom);

        const auto vertex_count = vertex_data.size() / 8;
        const auto index = static_cast<std::uint32_t>(vertex_count);
        const std::uint32_t indices[] = {index, index + 1, index + 2, index, index + 2, index + 3};

        const text_vertex_t vertices[] = {
            {x0, y0, glyph.uv_left,  glyph.uv_bottom, s_min, t_min, s_max, t_max},
            {x0, y1, glyph.uv_left,  glyph.uv_top,    s_min, t_min, s_max, t_max},
            {x1, y1, glyph.uv_right, glyph.uv_top,    s_min, t_min, s_max, t_max},
            {x1, y0, glyph.uv_right, glyph.uv_bottom, s_min, t_min, s_max, t_max}
        };

        index_data.insert(index_data.end(), indices, indices + 6);
        const auto* first = reinterpret_cast<const float*>(vertices);
        vertex_data.insert(vertex_data.end(), first, first + 4 * 8);

        pen->x += glyph.advance_x;
        previous = codepoint;
    }
}

void add_text_to_buffer(const char* text, glm::vec2* pen, thread_local_font_resources_t* res)
{
    if (!res || !res->m_buffer) {
        return;
    }
    add_text_to_vectors(
        text,
        pen,
        res->m_glyphs,
        res->m_kerning_px,
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
    font->pixel_height = pixel_height;
    font->font_digest = digest;

    std::uint32_t atlas_size = 0;
    if (!read(atlas_size)) {
        return nullptr;
    }
    font->atlas_size = static_cast<int>(atlas_size);

    if (!read(font->px_range) ||
        !read(font->baseline_offset_px) ||
        !read(font->monospace_advance_px))
    {
        return nullptr;
    }
    std::uint8_t mono_reliable = 0;
    std::uint8_t padding[3]{};
    if (!read(mono_reliable) || !in.read(reinterpret_cast<char*>(padding), sizeof(padding))) {
        return nullptr;
    }
    font->monospace_advance_reliable = (mono_reliable != 0);

    std::uint32_t glyph_count = 0;
    if (!read(glyph_count)) {
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
        font->glyphs.emplace(static_cast<char32_t>(code), g);
    }

    std::uint32_t kerning_count = 0;
    if (!read(kerning_count)) {
        return nullptr;
    }
    for (std::uint32_t i = 0; i < kerning_count; ++i) {
        msdf_kerning_key_t key{};
        float value = 0.f;
        if (!read(key.left) || !read(key.right) || !read(value)) {
            return nullptr;
        }
        font->kerning_px.emplace(key, value);
    }

    std::uint32_t atlas_bytes = 0;
    if (!read(atlas_bytes)) {
        return nullptr;
    }
    font->atlas_rgba.resize(atlas_bytes);
    if (!font->atlas_rgba.empty()) {
        in.read(reinterpret_cast<char*>(font->atlas_rgba.data()), atlas_bytes);
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
    write(static_cast<std::uint32_t>(font.pixel_height));
    out.write(reinterpret_cast<const char*>(font.font_digest.data()), font.font_digest.size());
    write(static_cast<std::uint32_t>(font.atlas_size));
    write(font.px_range);
    write(font.baseline_offset_px);
    write(font.monospace_advance_px);
    std::uint8_t mono_reliable = font.monospace_advance_reliable ? 1u : 0u;
    out.write(reinterpret_cast<const char*>(&mono_reliable), sizeof(mono_reliable));
    std::uint8_t padding[3]{0, 0, 0};
    out.write(reinterpret_cast<const char*>(padding), sizeof(padding));

    write(static_cast<std::uint32_t>(font.glyphs.size()));
    for (const auto& [code, g] : font.glyphs) {
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

    write(static_cast<std::uint32_t>(font.kerning_px.size()));
    for (const auto& [key, value] : font.kerning_px) {
        write(key.left);
        write(key.right);
        write(value);
    }

    write(static_cast<std::uint32_t>(font.atlas_rgba.size()));
    if (!font.atlas_rgba.empty()) {
        out.write(reinterpret_cast<const char*>(font.atlas_rgba.data()), static_cast<std::streamsize>(font.atlas_rgba.size()));
    }
}

std::shared_ptr<cached_font_data_t> build_font_cache(
    int pixel_height,
    const Sha256::Digest& font_digest,
    const std::function<void(const std::string&)>& log_error,
    const std::function<void(const std::string&)>& log_debug)
{
    auto font = std::make_shared<cached_font_data_t>();
    font->pixel_height = pixel_height;
    font->font_digest = font_digest;

    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) {
        if (log_error) {
            log_error("Failed to initialize FreeType for msdfgen");
        }
        return nullptr;
    }

    msdfgen::FontHandle* font_handle = msdfgen::loadFontData(
        ft,
        reinterpret_cast<const msdfgen::byte*>(s_font_storage.data()),
        static_cast<int>(s_font_storage.size()));
    if (!font_handle) {
        if (log_error) {
            log_error("Failed to load font data for msdfgen");
        }
        msdfgen::deinitializeFreetype(ft);
        return nullptr;
    }

    msdfgen::FontMetrics metrics{};
    if (!msdfgen::getFontMetrics(metrics, font_handle)) {
        if (log_error) {
            log_error("Failed to query font metrics for msdfgen");
        }
        msdfgen::destroyFont(font_handle);
        msdfgen::deinitializeFreetype(ft);
        return nullptr;
    }

    const std::vector<char32_t>& glyphs = glyph_codepoints();

    const double bitmap_scale = std::max(static_cast<double>(pixel_height), k_min_atlas_font_size) / metrics.ascenderY;
    const double draw_scale = static_cast<double>(pixel_height) / metrics.ascenderY;
    const double screen_to_atlas_ratio = draw_scale / bitmap_scale;

    const float atlas_px_range = k_atlas_px_range;
    const float sharpness_bias = k_sharpness_bias;

    font->px_range = (atlas_px_range * static_cast<float>(screen_to_atlas_ratio)) * sharpness_bias;
    font->baseline_offset_px = static_cast<float>(-metrics.descenderY * draw_scale);

    const int atlas_size = k_atlas_texture_size;
    std::vector<std::uint8_t> atlas_data(static_cast<std::size_t>(atlas_size) * atlas_size * 4, 0);
    int pen_x = 0;
    int pen_y = 0;
    int row_h = 0;

    for (const char32_t cp : glyphs) {
        msdfgen::Shape shape;
        double advance = 0.0;
        if (!msdfgen::loadGlyph(shape, font_handle, static_cast<msdfgen::unicode_t>(cp), &advance)) {
            continue;
        }
        msdfgen::edgeColoringSimple(shape, 3.0, 0);

        const auto bounds = shape.getBounds();
        const double width_em  = bounds.r - bounds.l;
        const double height_em = bounds.t - bounds.b;

        if (height_em <= 0.0 || width_em <= 0.0) {
            if (advance > 0.0) {
                msdf_glyph_t glyph{};
                glyph.advance_x = static_cast<float>(advance * draw_scale);
                glyph.plane_left = 0.0f;
                glyph.plane_bottom = 0.0f;
                glyph.plane_right = 0.0f;
                glyph.plane_top = 0.0f;
                glyph.uv_left = 0.0f;
                glyph.uv_bottom = 0.0f;
                glyph.uv_right = 0.0f;
                glyph.uv_top = 0.0f;
                font->glyphs.emplace(cp, glyph);
            }
            continue;
        }

        const double scaled_w = width_em * bitmap_scale;
        const double scaled_h = height_em * bitmap_scale;
        const int bitmap_w = static_cast<int>(std::ceil(scaled_w + atlas_px_range * 2.0));
        const int bitmap_h = static_cast<int>(std::ceil(scaled_h + atlas_px_range * 2.0));
        if (bitmap_w <= 0 || bitmap_h <= 0 || bitmap_w > atlas_size || bitmap_h > atlas_size) {
            continue;
        }

        if (pen_x + bitmap_w > atlas_size) {
            pen_x = 0;
            pen_y += row_h + 1;
            row_h = 0;
        }
        if (pen_y + bitmap_h > atlas_size) {
            if (log_debug) {
                log_debug("MSDF atlas out of space, skipping glyph");
            }
            break;
        }

        msdfgen::Bitmap<float, 4> bitmap(bitmap_w, bitmap_h);
        const msdfgen::Vector2 msdf_scale(bitmap_scale, bitmap_scale);

        const msdfgen::Vector2 msdf_translate(
            -bounds.l + (atlas_px_range / bitmap_scale),
            -bounds.b + (atlas_px_range / bitmap_scale)
        );

        const msdfgen::Projection projection(msdf_scale, msdf_translate);

        msdfgen::generateMTSDF(bitmap, shape, projection, atlas_px_range);

        for (int y = 0; y < bitmap_h; ++y) {
            for (int x = 0; x < bitmap_w; ++x) {
                const auto& px = bitmap(x, y);
                const int dst_idx = ((pen_y + y) * atlas_size + (pen_x + x)) * 4;
                for (int c = 0; c < 4; ++c) {
                    float norm = std::clamp(px[c], 0.0f, 1.0f);
                    atlas_data[dst_idx + c] = static_cast<std::uint8_t>(std::round(norm * 255.0f));
                }
            }
        }

        msdf_glyph_t glyph{};
        glyph.advance_x = static_cast<float>(advance * draw_scale);

        glyph.plane_left   = static_cast<float>( bounds.l * draw_scale - atlas_px_range * screen_to_atlas_ratio);
        glyph.plane_right  = static_cast<float>( bounds.r * draw_scale + atlas_px_range * screen_to_atlas_ratio);
        glyph.plane_top    = static_cast<float>(-bounds.b * draw_scale + atlas_px_range * screen_to_atlas_ratio);
        glyph.plane_bottom = static_cast<float>(-bounds.t * draw_scale - atlas_px_range * screen_to_atlas_ratio);

        auto uv_width  = (bounds.r - bounds.l) * bitmap_scale + 2 * atlas_px_range;
        auto uv_height = (bounds.t - bounds.b) * bitmap_scale + 2 * atlas_px_range;

        glyph.uv_left   = static_cast<float>(pen_x)             / atlas_size;
        glyph.uv_right  = static_cast<float>(pen_x + uv_width)  / atlas_size;
        glyph.uv_top    = static_cast<float>(pen_y)             / atlas_size;
        glyph.uv_bottom = static_cast<float>(pen_y + uv_height) / atlas_size;

        font->glyphs.emplace(cp, glyph);

        if (cp == static_cast<char32_t>('0')) {
            font->monospace_advance_px = glyph.advance_x;
            font->monospace_advance_reliable = (glyph.advance_x > 0.f);
        }

        row_h = std::max(row_h, bitmap_h);
        pen_x += bitmap_w + 1;
    }

    for (const char32_t left : glyphs) {
        for (const char32_t right : glyphs) {
            double k = 0.0;
            if (msdfgen::getKerning(k, font_handle, static_cast<msdfgen::unicode_t>(left), static_cast<msdfgen::unicode_t>(right))) {
                const float kern_px = static_cast<float>(k * draw_scale);
                if (kern_px != 0.f) {
                    font->kerning_px.emplace(msdf_kerning_key_t{left, right}, kern_px);
                }
            }
        }
    }

    font->atlas_size = atlas_size;
    font->atlas_rgba = std::move(atlas_data);
    font->cache_epoch = s_next_cache_epoch.fetch_add(1, std::memory_order_relaxed);

    msdfgen::destroyFont(font_handle);
    msdfgen::deinitializeFreetype(ft);

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

    auto cached = get_cached_font(pixel_height);
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

struct Text_block_std140
{
    float pmv[16] = {};
    float color[4] = {};
    float px_range = 0.f;
    float padding[3] = {};
};

static_assert(offsetof(Text_block_std140, pmv)      ==  0, "Text UBO pmv offset");
static_assert(offsetof(Text_block_std140, color)    == 64, "Text UBO color offset");
static_assert(offsetof(Text_block_std140, px_range) == 80, "Text UBO px_range offset");
static_assert(sizeof(Text_block_std140)             == 96, "Text UBO std140 size");

constexpr std::uint32_t k_text_ubo_bytes = sizeof(Text_block_std140);

QShader load_qsb(const char* alias)
{
    QFile file(QStringLiteral(":/vnm_plot/shaders/qsb/") + QString::fromLatin1(alias));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

struct rhi_text_call_t
{
    std::unique_ptr<QRhiBuffer> ubo;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    QRhiBuffer*      srb_last_ubo     = nullptr;
    QRhiTexture*     srb_last_texture = nullptr;
    QRhiSampler*     srb_last_sampler = nullptr;
};

struct rhi_text_draw_op_t
{
    std::uint32_t call_index  = 0;
    std::uint32_t index_start = 0;
    std::uint32_t index_count = 0;
    text_scissor_t scissor;
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
    resources.m_monospace_advance_px = cached.monospace_advance_px;
    resources.m_monospace_advance_reliable = cached.monospace_advance_reliable;
    resources.m_px_range = cached.px_range;
    resources.m_baseline_offset_px = cached.baseline_offset_px;
    resources.m_glyphs = cached.glyphs;
    resources.m_kerning_px = cached.kerning_px;
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
    const auto* res = m_impl->m_resources;
    const auto* cached = m_impl->m_font_cache.get();
    if (!text || (!res && !cached)) {
        return 0.0f;
    }
    const auto& glyphs = res ? res->m_glyphs : cached->glyphs;
    const auto& kerning_px = res ? res->m_kerning_px : cached->kerning_px;
    float x = 0.0f;
    char32_t previous = 0;
    const auto codepoints = utf8_to_codepoints(text);
    for (const auto codepoint : codepoints) {
        const auto g_it = glyphs.find(codepoint);
        if (g_it == glyphs.end()) {
            continue;
        }
        if (previous != 0) {
            const msdf_kerning_key_t key{previous, codepoint};
            const auto k_it = kerning_px.find(key);
            if (k_it != kerning_px.end()) {
                x += k_it->second;
            }
        }
        x += g_it->second.advance_x;
        previous = codepoint;
    }
    return x;
}

std::uint64_t Font_renderer::text_measure_cache_key() const
{
    const auto* res = m_impl->m_resources;
    if (res) {
        return res->m_cache_epoch;
    }
    const auto* cached = m_impl->m_font_cache.get();
    return cached ? cached->cache_epoch : 0;
}

float Font_renderer::monospace_advance_px() const
{
    const auto* res = m_impl->m_resources;
    if (res) {
        return res->m_monospace_advance_px;
    }
    const auto* cached = m_impl->m_font_cache.get();
    return cached ? cached->monospace_advance_px : 0.f;
}

bool Font_renderer::monospace_advance_is_reliable() const
{
    const auto* res = m_impl->m_resources;
    if (res) {
        return res->m_monospace_advance_reliable;
    }
    const auto* cached = m_impl->m_font_cache.get();
    return cached ? cached->monospace_advance_reliable : false;
}

float Font_renderer::compute_numeric_bottom() const
{
    const auto* res = m_impl->m_resources;
    const auto* cached = m_impl->m_font_cache.get();
    if (!res && !cached) {
        return 0.0f;
    }
    const auto& glyphs = res ? res->m_glyphs : cached->glyphs;
    static const char* k_sample = "0123456789-+.,";
    float max_bottom = -std::numeric_limits<float>::infinity();
    for (const char* p = k_sample; *p; ++p) {
        const auto it = glyphs.find(static_cast<unsigned char>(*p));
        if (it != glyphs.end()) {
            const float neg_bottom = -(it->second.plane_bottom);
            if (neg_bottom > max_bottom) {
                max_bottom = neg_bottom;
            }
        }
    }
    return max_bottom;
}

float Font_renderer::baseline_offset_px() const
{
    const auto* res = m_impl->m_resources;
    if (res) {
        return res->m_baseline_offset_px;
    }
    const auto* cached = m_impl->m_font_cache.get();
    return cached ? cached->baseline_offset_px : 0.f;
}

void Font_renderer::batch_text(float x, float y, const char* text)
{
    if (m_impl->m_rhi_batch_active) {
        const auto* cached = m_impl->m_font_cache.get();
        if (!cached) {
            return;
        }
        glm::vec2 pen{x, y};
        add_text_to_vectors(
            text,
            &pen,
            cached->glyphs,
            cached->kerning_px,
            m_impl->m_rhi_vertex_data,
            m_impl->m_rhi_index_data);
        return;
    }

    auto* res = m_impl->m_resources;
    if (!res) {
        return;
    }
    glm::vec2 pen{x, y};
    add_text_to_buffer(text, &pen, res);
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
    const text_scissor_t& scissor)
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
    const std::uint32_t index_start =
        static_cast<std::uint32_t>(m_impl->m_rhi_frame_index_data.size());
    const std::uint32_t base_vertex =
        static_cast<std::uint32_t>(vertex_start_float_count / 8);
    m_impl->m_rhi_frame_vertex_data.insert(
        m_impl->m_rhi_frame_vertex_data.end(),
        m_impl->m_rhi_vertex_data.begin(),
        m_impl->m_rhi_vertex_data.end());
    m_impl->m_rhi_frame_index_data.reserve(
        m_impl->m_rhi_frame_index_data.size() + m_impl->m_rhi_index_data.size());
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
        rhi_state.atlas_size != cached.atlas_size ||
        rhi_state.uploaded_cache_epoch != cached.cache_epoch)
    {
        rhi_state.atlas_texture.reset(rhi->newTexture(
            QRhiTexture::RGBA8,
            QSize(cached.atlas_size, cached.atlas_size)));
        if (!rhi_state.atlas_texture || !rhi_state.atlas_texture->create()) {
            rhi_state.atlas_texture.reset();
            m_impl->m_rhi_vertex_data.clear();
            m_impl->m_rhi_index_data.clear();
            return;
        }
        QImage image(
            cached.atlas_rgba.data(),
            cached.atlas_size,
            cached.atlas_size,
            cached.atlas_size * 4,
            QImage::Format_RGBA8888);
        updates->uploadTexture(rhi_state.atlas_texture.get(), image);
        rhi_state.atlas_size = cached.atlas_size;
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
            m_impl->m_rhi_vertex_data.clear();
            m_impl->m_rhi_index_data.clear();
            return;
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
            m_impl->m_rhi_vertex_data.clear();
            m_impl->m_rhi_index_data.clear();
            return;
        }
        call.srb_last_ubo     = call.ubo.get();
        call.srb_last_texture = rhi_state.atlas_texture.get();
        call.srb_last_sampler = rhi_state.sampler.get();
    }

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
                call.ubo.get(),
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

    Text_block_std140 block{};
    std::memcpy(block.pmv, glm::value_ptr(pmv), sizeof(block.pmv));
    block.color[0] = color.r;
    block.color[1] = color.g;
    block.color[2] = color.b;
    block.color[3] = color.a;
    block.px_range = cached.px_range;
    updates->updateDynamicBuffer(call.ubo.get(), 0, sizeof(block), &block);

    rhi_text_draw_op_t op{};
    op.call_index  = static_cast<std::uint32_t>(call_index);
    op.index_start = index_start;
    op.index_count = static_cast<std::uint32_t>(m_impl->m_rhi_index_data.size());
    op.scissor     = scissor;
    rhi_state.ops.push_back(op);

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

    const std::size_t vertex_bytes =
        m_impl->m_rhi_frame_vertex_data.size() * sizeof(float);
    const std::size_t index_bytes =
        m_impl->m_rhi_frame_index_data.size() * sizeof(std::uint32_t);

    if (!rhi_state.vbo || rhi_state.vbo_capacity_bytes < vertex_bytes) {
        const std::size_t alloc = vertex_bytes + vertex_bytes / 4;
        rhi_state.vbo.reset(rhi->newBuffer(
            QRhiBuffer::Dynamic,
            QRhiBuffer::VertexBuffer,
            static_cast<quint32>(alloc)));
        if (!rhi_state.vbo || !rhi_state.vbo->create()) {
            rhi_state.vbo.reset();
            rhi_reset_frame();
            return;
        }
        rhi_state.vbo_capacity_bytes = alloc;
    }

    if (!rhi_state.ibo || rhi_state.ibo_capacity_bytes < index_bytes) {
        const std::size_t alloc = index_bytes + index_bytes / 4;
        rhi_state.ibo.reset(rhi->newBuffer(
            QRhiBuffer::Dynamic,
            QRhiBuffer::IndexBuffer,
            static_cast<quint32>(alloc)));
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
        static_cast<quint32>(vertex_bytes),
        m_impl->m_rhi_frame_vertex_data.data());
    updates->updateDynamicBuffer(
        rhi_state.ibo.get(),
        0,
        static_cast<quint32>(index_bytes),
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
    for (const auto& op : rhi_state.ops) {
        if (op.call_index >= rhi_state.calls.size() || op.index_count == 0) {
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
