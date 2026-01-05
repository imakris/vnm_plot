#pragma once

// VNM Plot Library - UTF-8 Utilities
// Qt-free UTF-8 string processing utilities.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// UTF-8 to Unicode Code Point Conversion
// -----------------------------------------------------------------------------

// Decode a single UTF-8 code point from a string.
// Returns the code point and advances the iterator.
// Returns 0xFFFD (replacement character) on invalid sequences.
[[nodiscard]]
char32_t utf8_decode_one(const char*& it, const char* end) noexcept;

// Convert a UTF-8 string to a vector of Unicode code points (UCS-4).
// Replaces invalid sequences with U+FFFD.
[[nodiscard]]
std::vector<char32_t> utf8_to_codepoints(std::string_view utf8);

// Convert a single Unicode code point to UTF-8.
// Returns the number of bytes written (1-4), or 0 on invalid code point.
// Output buffer must have at least 4 bytes.
size_t codepoint_to_utf8(char32_t cp, char* out) noexcept;

// Convert a vector of code points back to UTF-8.
[[nodiscard]]
std::string codepoints_to_utf8(const std::vector<char32_t>& codepoints);

// -----------------------------------------------------------------------------
// String Length Utilities
// -----------------------------------------------------------------------------

// Count the number of Unicode code points in a UTF-8 string.
// Invalid sequences count as one code point each.
[[nodiscard]]
size_t utf8_length(std::string_view utf8) noexcept;

// -----------------------------------------------------------------------------
// Character Classification
// -----------------------------------------------------------------------------

// Check if a code point is a printable character
[[nodiscard]]
bool is_printable(char32_t cp) noexcept;

// Check if a code point is whitespace
[[nodiscard]]
bool is_whitespace(char32_t cp) noexcept;

// Check if a code point is a digit (0-9)
[[nodiscard]]
bool is_digit(char32_t cp) noexcept;

// -----------------------------------------------------------------------------
// Simple String Operations (ASCII-focused, UTF-8 safe)
// -----------------------------------------------------------------------------

// Trim whitespace from both ends of a UTF-8 string
[[nodiscard]]
std::string_view trim(std::string_view str) noexcept;

// Split a string by a delimiter character (ASCII only)
[[nodiscard]]
std::vector<std::string_view> split(std::string_view str, char delim);

} // namespace vnm::plot
