#include <vnm_plot/core/font_renderer.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/gl_program.h>
#include "platform_paths.h"
#include "sha256.h"
#include "utf8_utils.h"
#include "tls_registry.h"

#ifdef GL_GLEXT_VERSION
#undef GL_GLEXT_VERSION
#endif

#include <glatter/glatter.h>
#include <glm/gtc/type_ptr.hpp>
#include <msdfgen.h>
#include <msdfgen-ext.h>

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
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    std::vector<float> vertex_data; // 8 floats per vertex: position, tex coord, tex bounds
    std::vector<GLuint> index_data;
};

vertex_buffer_t* vertex_buffer_new(const char*)
{
    auto* buffer = new vertex_buffer_t();
    glGenVertexArrays(1, &buffer->vao);
    glGenBuffers(1, &buffer->vbo);
    glGenBuffers(1, &buffer->ebo);

    glBindVertexArray(buffer->vao);

    glBindBuffer(GL_ARRAY_BUFFER, buffer->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->ebo);

    const GLsizei stride = sizeof(float) * 8;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 2));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 4));

    glBindVertexArray(0);

    return buffer;
}

void vertex_buffer_delete(vertex_buffer_t* buffer)
{
    if (!buffer) {
        return;
    }

    if (buffer->vao) {
        glDeleteVertexArrays(1, &buffer->vao);
    }
    if (buffer->vbo) {
        glDeleteBuffers(1, &buffer->vbo);
    }
    if (buffer->ebo) {
        glDeleteBuffers(1, &buffer->ebo);
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

void vertex_buffer_push_back_indices(vertex_buffer_t* buffer, const GLuint* indices, std::size_t count)
{
    if (!buffer || !indices || count == 0) {
        return;
    }

    buffer->index_data.insert(buffer->index_data.end(), indices, indices + count);
}

void vertex_buffer_render(vertex_buffer_t* buffer, GLenum mode)
{
    if (!buffer || buffer->index_data.empty() || buffer->vertex_data.empty()) {
        return;
    }

    glBindVertexArray(buffer->vao);

    glBindBuffer(GL_ARRAY_BUFFER, buffer->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(buffer->vertex_data.size() * sizeof(float)),
                 buffer->vertex_data.data(),
                 GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(buffer->index_data.size() * sizeof(GLuint)),
                 buffer->index_data.data(),
                 GL_DYNAMIC_DRAW);

    glDrawElements(mode,
                   static_cast<GLsizei>(buffer->index_data.size()),
                   GL_UNSIGNED_INT,
                   nullptr);

    glBindVertexArray(0);
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

// --- Thread-Local OpenGL Resources ---
// This struct holds the GL resources that must be unique per-thread.
struct thread_local_font_resources_t
{
    GLuint m_texture = 0;
    vertex_buffer_t* m_buffer = nullptr;
    std::unique_ptr<GL_program> m_shader_program;
    int m_pixel_height = 0;
    GLint m_tex_uniform_location = -1;
    GLint m_color_uniform_location = -1;
    GLint m_pmv_uniform_location = -1;
    GLint m_range_uniform_location = -1;
    std::uint64_t m_cache_epoch = 0;
    float m_monospace_advance_px = 0.f;
    bool m_monospace_advance_reliable = false;
    float m_px_range = std::numeric_limits<float>::quiet_NaN();
    float m_baseline_offset_px = 0.f;

    std::unordered_map<char32_t, msdf_glyph_t> m_glyphs;
    std::unordered_map<msdf_kerning_key_t, float, msdf_kerning_key_hash_t, msdf_kerning_key_eq_t> m_kerning_px;

    ~thread_local_font_resources_t() = default;

    void destroy_gl()
    {
        if (m_texture) {
            glDeleteTextures(1, &m_texture);
            m_texture = 0;
        }
        if (m_buffer) {
            vertex_buffer_delete(m_buffer);
            m_buffer = nullptr;
        }
        m_shader_program.reset();
        m_tex_uniform_location = -1;
        m_color_uniform_location = -1;
        m_pmv_uniform_location = -1;
        m_range_uniform_location = -1;
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

void add_text_to_buffer(const char* text, glm::vec2* pen, thread_local_font_resources_t* res)
{
    const auto codepoints = utf8_to_codepoints(text);
    char32_t previous = 0;
    for (const auto codepoint : codepoints) {
        const auto g_it = res->m_glyphs.find(codepoint);
        if (g_it == res->m_glyphs.end()) {
            continue;
        }

        if (previous != 0) {
            const msdf_kerning_key_t key{previous, codepoint};
            const auto k_it = res->m_kerning_px.find(key);
            if (k_it != res->m_kerning_px.end()) {
                pen->x += k_it->second;
            }
        }

        const msdf_glyph_t& glyph = g_it->second;
        const float x0 = pen->x + glyph.plane_left;
        const float x1 = pen->x + glyph.plane_right;
        const float y0 = pen->y + glyph.plane_bottom;
        const float y1 = pen->y + glyph.plane_top;

        const auto vertex_count = vertex_buffer_vertex_count(res->m_buffer);
        const GLuint index = static_cast<GLuint>(vertex_count);
        const GLuint indices[] = {index, index + 1, index + 2, index, index + 2, index + 3};

        struct vertex_t
        {
            float x, y;
            float s, t;
            float s_min, t_min, s_max, t_max;
        };

        const vertex_t vertices[] = {
            {x0, y0, glyph.uv_left,  glyph.uv_bottom, glyph.uv_left,  glyph.uv_bottom, glyph.uv_right, glyph.uv_top},
            {x0, y1, glyph.uv_left,  glyph.uv_top,    glyph.uv_left,  glyph.uv_bottom, glyph.uv_right, glyph.uv_top},
            {x1, y1, glyph.uv_right, glyph.uv_top,    glyph.uv_left,  glyph.uv_bottom, glyph.uv_right, glyph.uv_top},
            {x1, y0, glyph.uv_right, glyph.uv_bottom, glyph.uv_left,  glyph.uv_bottom, glyph.uv_right, glyph.uv_top}
        };
        vertex_buffer_push_back_indices(res->m_buffer, indices, 6);
        vertex_buffer_push_back_vertices(res->m_buffer, vertices, 4);
        pen->x += glyph.advance_x;
        previous = codepoint;
    }
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

} // anonymous namespace

// --- PIMPL Definition ---
struct Font_renderer::impl_t
{
    thread_local_font_resources_t* m_resources = nullptr;
    std::function<void(const std::string&)> m_log_error;
    std::function<void(const std::string&)> m_log_debug;
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

bool Font_renderer::is_initialized() const
{
    return m_impl->m_resources != nullptr;
}

void Font_renderer::initialize(Asset_loader& asset_loader, int pixel_height, bool force_rebuild)
{
    auto& resources = thread_local_resources();
    const bool tls_ready = (resources.m_pixel_height == pixel_height) &&
                           (resources.m_texture != 0) &&
                           (resources.m_shader_program != nullptr);
    if (!force_rebuild && tls_ready) {
        m_impl->m_resources = &resources;
        return;
    }

    if (s_font_storage.empty()) {
        std::lock_guard<std::mutex> locker(s_font_storage_mutex);
        if (s_font_storage.empty()) {
            auto font_data = asset_loader.load("fonts/monospace.ttf");
            if (font_data) {
                s_font_storage.assign(font_data->begin(), font_data->end());
            }
        }
    }

    resources.destroy_gl();

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
        cached = build_font_cache(pixel_height, font_digest, m_impl->m_log_error, m_impl->m_log_debug);
        if (cached) {
            store_cached_font(cached);
            if (disk_cache) {
                const auto cache_path = cache_file_path(pixel_height, font_digest);
                save_cached_font_to_disk(cache_path, *cached);
            }
        }
    }
    if (!cached) {
        return;
    }

    auto* const res = &resources;
    res->m_pixel_height = pixel_height;
    res->m_cache_epoch = cached->cache_epoch;

    res->m_buffer = vertex_buffer_new("vertex:2f,tex_coord:2f,tex_bounds:4f");
    res->m_monospace_advance_px = cached->monospace_advance_px;
    res->m_monospace_advance_reliable = cached->monospace_advance_reliable;
    res->m_px_range = cached->px_range;
    res->m_baseline_offset_px = cached->baseline_offset_px;
    res->m_glyphs = cached->glyphs;
    res->m_kerning_px = cached->kerning_px;
    res->m_texture = 0;

    glGenTextures(1, &res->m_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, res->m_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA,
        cached->atlas_size, cached->atlas_size, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, cached->atlas_rgba.data()
    );

    // Load and compile MSDF text shader
    auto shader_sources = asset_loader.load_shader("shaders/msdf_text");
    if (shader_sources) {
        std::string vert_src(shader_sources->vertex.begin(), shader_sources->vertex.end());
        std::string frag_src(shader_sources->fragment.begin(), shader_sources->fragment.end());

        auto sp = create_gl_program(vert_src, "", frag_src, m_impl->m_log_error);
        if (sp) {
            res->m_shader_program = std::move(sp);
            const GLuint program_id = res->m_shader_program->program_id();
            res->m_tex_uniform_location = glGetUniformLocation(program_id, "tex");
            res->m_color_uniform_location = glGetUniformLocation(program_id, "color");
            res->m_pmv_uniform_location = glGetUniformLocation(program_id, "pmv");
            res->m_range_uniform_location = glGetUniformLocation(program_id, "px_range");
        }
    }

    m_impl->m_resources = &thread_local_resources();
}

void Font_renderer::deinitialize()
{
    m_impl->m_resources = nullptr;
}

void Font_renderer::cleanup_thread_resources()
{
    auto& resources = thread_local_resources();
    resources.destroy_gl();
}

void Font_renderer::shutdown_all_thread_resources()
{
    font_registry().shutdown([](thread_local_font_resources_t& entry) {
        entry.destroy_gl();
    });
}

float Font_renderer::measure_text_px(const char* text) const
{
    const auto* res = m_impl->m_resources;
    if (!res || !text) {
        return 0.0f;
    }
    float x = 0.0f;
    char32_t previous = 0;

    // Use streaming UTF-8 decoder to avoid vector allocation.
    // This is called frequently during layout, so avoiding heap
    // allocations provides measurable performance improvement.
    const char* it = text;
    const char* end = text + std::strlen(text);
    while (it < end) {
        const char32_t codepoint = utf8_decode_one(it, end);
        if (codepoint == 0) {
            break;
        }
        const auto g_it = res->m_glyphs.find(codepoint);
        if (g_it == res->m_glyphs.end()) {
            continue;
        }
        if (previous != 0) {
            const msdf_kerning_key_t key{previous, codepoint};
            const auto k_it = res->m_kerning_px.find(key);
            if (k_it != res->m_kerning_px.end()) {
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
    return res ? res->m_cache_epoch : 0;
}

float Font_renderer::monospace_advance_px() const
{
    const auto* res = m_impl->m_resources;
    return res ? res->m_monospace_advance_px : 0.f;
}

bool Font_renderer::monospace_advance_is_reliable() const
{
    const auto* res = m_impl->m_resources;
    return res ? res->m_monospace_advance_reliable : false;
}

float Font_renderer::compute_numeric_bottom() const
{
    const auto* res = m_impl->m_resources;
    if (!res) {
        return 0.0f;
    }
    static const char* k_sample = "0123456789-+.,";
    float max_bottom = -std::numeric_limits<float>::infinity();
    for (const char* p = k_sample; *p; ++p) {
        const auto it = res->m_glyphs.find(static_cast<unsigned char>(*p));
        if (it != res->m_glyphs.end()) {
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
    return res ? res->m_baseline_offset_px : 0.f;
}

void Font_renderer::batch_text(float x, float y, const char* text)
{
    auto* res = m_impl->m_resources;
    if (!res) {
        return;
    }
    glm::vec2 pen{x, y};
    add_text_to_buffer(text, &pen, res);
}

void Font_renderer::draw_and_flush(const glm::mat4& pmv, const glm::vec4& color)
{
    const auto* res = m_impl->m_resources;
    if (!res || !res->m_shader_program) {
        return;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, res->m_texture);
    res->m_shader_program->bind();

    if (res->m_tex_uniform_location >= 0) {
        glUniform1i(res->m_tex_uniform_location, 0);
    }

    if (res->m_color_uniform_location < 0 || res->m_pmv_uniform_location < 0) {
        return;
    }

    if (res->m_range_uniform_location >= 0) {
        glUniform1f(res->m_range_uniform_location, res->m_px_range);
    }
    glUniform4fv(res->m_color_uniform_location, 1, glm::value_ptr(color));
    glUniformMatrix4fv(res->m_pmv_uniform_location, 1, GL_FALSE, glm::value_ptr(pmv));
    vertex_buffer_render(res->m_buffer, GL_TRIANGLES);
    vertex_buffer_clear(res->m_buffer);
}

void Font_renderer::clear_buffer()
{
    const auto* res = m_impl->m_resources;
    if (res && res->m_buffer) {
        vertex_buffer_clear(res->m_buffer);
    }
}

} // namespace vnm::plot
