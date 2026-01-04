#include <vnm_plot/core/asset_loader.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <utility>

namespace vnm::plot::core {

Asset_loader::Asset_loader() = default;
Asset_loader::~Asset_loader() = default;

void Asset_loader::set_log_callback(LogCallback callback)
{
    m_log_callback = std::move(callback);
}

void Asset_loader::log_error(const std::string& message) const
{
    if (m_log_callback) {
        m_log_callback(message);
    }
}

void Asset_loader::set_override_directory(std::string_view path)
{
    m_override_dir = std::string(path);
}

std::string_view Asset_loader::override_directory() const noexcept
{
    return m_override_dir;
}

void Asset_loader::register_embedded(std::string_view name, std::string_view data)
{
    m_embedded[std::string(name)] = data;
}

bool Asset_loader::load_file(std::string_view path, ByteBuffer& out) const
{
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    const auto pos = file.tellg();
    if (pos < 0) {
        return false;
    }

    const auto size = static_cast<std::streamsize>(pos);
    out.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);

    if (!file.read(out.data(), size)) {
        out.clear();
        return false;
    }

    return true;
}

std::optional<ByteBuffer> Asset_loader::load(std::string_view name) const
{
    // First, try override directory
    if (!m_override_dir.empty()) {
        std::filesystem::path override_path = m_override_dir;
        override_path /= name;

        if (std::filesystem::exists(override_path)) {
            ByteBuffer buffer;
            if (load_file(override_path.string(), buffer)) {
                return buffer;
            }
            log_error("Failed to read override file: " + override_path.string());
        }
    }

    // Fall back to embedded assets
    auto it = m_embedded.find(std::string(name));
    if (it != m_embedded.end()) {
        return ByteBuffer(it->second);
    }

    log_error("Asset not found: " + std::string(name));
    return std::nullopt;
}

std::optional<ByteView> Asset_loader::load_embedded_view(std::string_view name) const
{
    auto it = m_embedded.find(std::string(name));
    if (it != m_embedded.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool Asset_loader::exists(std::string_view name) const
{
    // Check override directory
    if (!m_override_dir.empty()) {
        std::filesystem::path override_path = m_override_dir;
        override_path /= name;
        if (std::filesystem::exists(override_path)) {
            return true;
        }
    }

    // Check embedded
    return m_embedded.find(std::string(name)) != m_embedded.end();
}

std::optional<Asset_loader::ShaderSources> Asset_loader::load_shader(std::string_view base_name) const
{
    ShaderSources sources;

    const std::string base(base_name);

    // Load vertex shader (required)
    auto vert = load(base + ".vert");
    if (!vert) {
        log_error("Missing vertex shader: " + base + ".vert");
        return std::nullopt;
    }
    sources.vertex = std::move(*vert);

    // Load geometry shader (optional)
    if (exists(base + ".geom")) {
        auto geom = load(base + ".geom");
        if (geom) {
            sources.geometry = std::move(*geom);
        }
    }

    // Load fragment shader (required)
    auto frag = load(base + ".frag");
    if (!frag) {
        log_error("Missing fragment shader: " + base + ".frag");
        return std::nullopt;
    }
    sources.fragment = std::move(*frag);

    return sources;
}

// -----------------------------------------------------------------------------
// Global default asset loader
// -----------------------------------------------------------------------------

namespace {

std::once_flag s_embedded_assets_init_flag;

Asset_loader& get_default_loader_instance()
{
    static Asset_loader instance;
    return instance;
}

} // anonymous namespace

Asset_loader& default_asset_loader()
{
    auto& loader = get_default_loader_instance();
    // Auto-register embedded assets on first access
    std::call_once(s_embedded_assets_init_flag, []() {
        init_embedded_assets();
    });
    return loader;
}

// Note: init_embedded_assets() is defined in the generated embedded_assets.cpp

} // namespace vnm::plot::core
