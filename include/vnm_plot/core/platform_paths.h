#pragma once

// VNM Plot Library - Platform Paths
// Qt-free platform-specific path utilities for cache directories.

#include <filesystem>
#include <string>

namespace vnm::plot::core {

// -----------------------------------------------------------------------------
// Cache Directory
// -----------------------------------------------------------------------------

// Get the application-specific cache directory.
// Creates the directory if it doesn't exist.
// Returns empty path on failure.
//
// Platform-specific locations:
// - Windows: %LOCALAPPDATA%/vnm_plot/cache
// - macOS: ~/Library/Caches/vnm_plot
// - Linux: $XDG_CACHE_HOME/vnm_plot or ~/.cache/vnm_plot
[[nodiscard]] std::filesystem::path get_cache_directory();

// Get the application data directory (for persistent data).
// Creates the directory if it doesn't exist.
// Returns empty path on failure.
//
// Platform-specific locations:
// - Windows: %LOCALAPPDATA%/vnm_plot
// - macOS: ~/Library/Application Support/vnm_plot
// - Linux: $XDG_DATA_HOME/vnm_plot or ~/.local/share/vnm_plot
[[nodiscard]] std::filesystem::path get_data_directory();

// Set the application name used in directory paths (default: "vnm_plot")
void set_app_name(const std::string& name);

// Get the current application name
[[nodiscard]] const std::string& get_app_name();

} // namespace vnm::plot::core
