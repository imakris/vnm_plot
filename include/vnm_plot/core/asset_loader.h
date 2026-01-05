#pragma once

// VNM Plot Library - Asset Loader
// Qt-free asset loading with embedded defaults and optional file overrides.

#include "types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// Asset_loader
// -----------------------------------------------------------------------------
// Loads assets (shaders, fonts) with support for:
// - Embedded defaults (compiled into the binary)
// - Optional file system overrides (for development/debugging)
class Asset_loader
{
public:
    // Log callback for errors
    using LogCallback = std::function<void(const std::string&)>;

    Asset_loader();
    ~Asset_loader();

    // Set log callback
    void set_log_callback(LogCallback callback);

    // Configure file system override directory (empty to disable)
    // If set, assets will first be searched in this directory.
    void set_override_directory(std::string_view path);

    // Get the override directory (empty if not set)
    [[nodiscard]] std::string_view override_directory() const noexcept;

    // Register an embedded asset
    // The data must remain valid for the lifetime of the Asset_loader.
    void register_embedded(std::string_view name, std::string_view data);

    // Load an asset by name
    // Returns the asset data, or nullopt on failure.
    // First checks override directory, then embedded assets.
    [[nodiscard]] std::optional<ByteBuffer> load(std::string_view name) const;

    // Load an asset, returning a view if embedded (avoiding copy)
    // Returns nullopt if not found or if only available as file.
    [[nodiscard]] std::optional<ByteView> load_embedded_view(std::string_view name) const;

    // Check if an asset exists (in either embedded or override location)
    [[nodiscard]] bool exists(std::string_view name) const;

    // --- Convenience methods for shader loading ---

    // Load a shader program's sources (vertex, optional geometry, fragment)
    struct ShaderSources
    {
        ByteBuffer vertex;
        ByteBuffer geometry;  // May be empty
        ByteBuffer fragment;
    };

    // Load shader sources by base name (appends .vert, .geom, .frag)
    // geom is optional (may return empty string)
    [[nodiscard]] std::optional<ShaderSources> load_shader(std::string_view base_name) const;

private:
    bool load_file(std::string_view path, ByteBuffer& out) const;
    void log_error(const std::string& message) const;

    LogCallback m_log_callback;
    std::string m_override_dir;

    // Map from asset name to embedded data view
    std::unordered_map<std::string, std::string_view> m_embedded;
};

// -----------------------------------------------------------------------------
// Default Asset Registry
// -----------------------------------------------------------------------------
// Provides access to embedded default assets (shaders, fonts).
// Populated at library initialization.

// Get the global default asset loader with embedded assets.
// Embedded assets are auto-registered on first access.
[[nodiscard]] Asset_loader& default_asset_loader();

// Initialize embedded assets into the given loader.
// (Called internally by default_asset_loader; defined in generated embedded_assets.cpp)
void init_embedded_assets(Asset_loader& loader);

} // namespace vnm::plot
