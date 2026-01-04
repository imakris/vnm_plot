#pragma once

// VNM Plot Library - Core Types
// Qt-free replacement types for use in the core library.

#include <cstdint>
#include <string>
#include <string_view>

namespace vnm::plot::core {

// -----------------------------------------------------------------------------
// Size2i - Replacement for QSize
// -----------------------------------------------------------------------------
struct Size2i
{
    int width  = 0;
    int height = 0;

    constexpr Size2i() = default;
    constexpr Size2i(int w, int h) : width(w), height(h) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return width > 0 && height > 0;
    }

    [[nodiscard]] constexpr bool operator==(const Size2i& other) const noexcept
    {
        return width == other.width && height == other.height;
    }

    [[nodiscard]] constexpr bool operator!=(const Size2i& other) const noexcept
    {
        return !(*this == other);
    }
};

// -----------------------------------------------------------------------------
// Byte Buffer - Lightweight replacement for QByteArray
// -----------------------------------------------------------------------------
// Uses std::string for storage (compatible with std::string_view).
// std::string is guaranteed contiguous and null-terminated.
using ByteBuffer = std::string;

// For read-only views
using ByteView = std::string_view;

// -----------------------------------------------------------------------------
// Asset Reference
// -----------------------------------------------------------------------------
// Represents a reference to an asset (shader, font, etc.)
// Can be either embedded data or a file path.
//
// LIFETIME WARNING: AssetRef stores std::string_view, not owned strings.
// The caller must ensure that the underlying data outlives the AssetRef.
// For embedded assets, this is typically safe (static storage duration).
// For file paths, pass string literals or ensure the std::string outlives the AssetRef.
struct AssetRef
{
    enum class Type : uint8_t
    {
        Empty,
        Embedded,  // Data embedded in binary
        FilePath   // Path to external file
    };

    Type             type = Type::Empty;
    std::string_view data;  // For embedded: the data; for file path: the path
    std::string_view name;  // Human-readable name for debugging

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return type != Type::Empty && !data.empty();
    }

    [[nodiscard]] constexpr bool is_embedded() const noexcept
    {
        return type == Type::Embedded;
    }

    [[nodiscard]] constexpr bool is_file() const noexcept
    {
        return type == Type::FilePath;
    }

    // Create an embedded asset reference
    static constexpr AssetRef embedded(std::string_view embedded_data,
                                        std::string_view asset_name = {})
    {
        return AssetRef{Type::Embedded, embedded_data, asset_name};
    }

    // Create a file path asset reference
    static constexpr AssetRef file(std::string_view path,
                                    std::string_view asset_name = {})
    {
        return AssetRef{Type::FilePath, path, asset_name};
    }
};

} // namespace vnm::plot::core
