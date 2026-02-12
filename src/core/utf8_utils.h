#pragma once

// VNM Plot Library - UTF-8 Utilities
// Qt-free UTF-8 string processing utilities.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vnm::plot {

// Convert a UTF-8 string to a vector of Unicode code points (UCS-4).
// Replaces invalid sequences with U+FFFD.
[[nodiscard]]
std::vector<char32_t> utf8_to_codepoints(std::string_view utf8);

// Convert a vector of code points back to UTF-8.
[[nodiscard]]
std::string codepoints_to_utf8(const std::vector<char32_t>& codepoints);

} // namespace vnm::plot
