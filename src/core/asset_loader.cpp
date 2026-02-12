#include <vnm_plot/core/asset_loader.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

namespace vnm::plot {

Asset_loader::Asset_loader() = default;
Asset_loader::~Asset_loader() = default;

void Asset_loader::set_log_callback(Log_callback callback)
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

bool Asset_loader::load_file(std::string_view path, Byte_buffer& out) const
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

std::optional<Byte_buffer> Asset_loader::load(std::string_view name) const
{
    // First, try override directory
    if (!m_override_dir.empty()) {
        std::filesystem::path override_path = m_override_dir;
        override_path /= name;

        if (std::filesystem::exists(override_path)) {
            Byte_buffer buffer;
            if (load_file(override_path.string(), buffer)) {
                return buffer;
            }
            log_error("Failed to read override file: " + override_path.string());
        }
    }

    // Fall back to embedded assets
    auto it = m_embedded.find(std::string(name));
    if (it != m_embedded.end()) {
        return Byte_buffer(it->second);
    }

    log_error("Asset not found: " + std::string(name));
    return std::nullopt;
}

std::optional<Asset_loader::Shader_sources> Asset_loader::load_shader(std::string_view base_name) const
{
    Shader_sources sources;

    const std::string base(base_name);

    // Load vertex shader (required)
    auto vert = load(base + ".vert");
    if (!vert) {
        log_error("Missing vertex shader: " + base + ".vert");
        return std::nullopt;
    }
    sources.vertex = std::move(*vert);

    // Load geometry shader (optional — check existence first to avoid spurious error log)
    const std::string geom_name = base + ".geom";
    const bool geom_exists =
        m_embedded.find(geom_name) != m_embedded.end() ||
        (!m_override_dir.empty() &&
         std::filesystem::exists(std::filesystem::path(m_override_dir) / geom_name));
    if (geom_exists) {
        auto geom = load(geom_name);
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

} // namespace vnm::plot
