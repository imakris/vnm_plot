#include "utf8_utils.h"

#include <algorithm>

namespace vnm::plot {

namespace {

constexpr char32_t k_replacement_char = 0xFFFD;

// Check if byte is a UTF-8 continuation byte (10xxxxxx)
constexpr bool is_continuation(unsigned char c) noexcept
{
    return (c & 0xC0) == 0x80;
}

} // anonymous namespace

char32_t utf8_decode_one(const char*& it, const char* end) noexcept
{
    if (it >= end) {
        return k_replacement_char;
    }

    const auto* p = reinterpret_cast<const unsigned char*>(it);
    const unsigned char c0 = *p++;

    // ASCII (0xxxxxxx)
    if ((c0 & 0x80) == 0) {
        it = reinterpret_cast<const char*>(p);
        return static_cast<char32_t>(c0);
    }

    // Determine sequence length and validate first byte
    size_t seq_len;
    char32_t cp;

    if ((c0 & 0xE0) == 0xC0) {
        // 2-byte sequence (110xxxxx)
        seq_len = 2;
        cp = c0 & 0x1F;
    }
    else if ((c0 & 0xF0) == 0xE0) {
        // 3-byte sequence (1110xxxx)
        seq_len = 3;
        cp = c0 & 0x0F;
    }
    else if ((c0 & 0xF8) == 0xF0) {
        // 4-byte sequence (11110xxx)
        seq_len = 4;
        cp = c0 & 0x07;
    }
    else {
        // Invalid start byte
        it = reinterpret_cast<const char*>(p);
        return k_replacement_char;
    }

    // Check if we have enough bytes
    const auto* seq_end = reinterpret_cast<const unsigned char*>(end);
    if (p + seq_len - 1 > seq_end) {
        it = end;
        return k_replacement_char;
    }

    // Decode continuation bytes
    for (size_t i = 1; i < seq_len; ++i) {
        if (!is_continuation(*p)) {
            it = reinterpret_cast<const char*>(p);
            return k_replacement_char;
        }
        cp = (cp << 6) | (*p++ & 0x3F);
    }

    it = reinterpret_cast<const char*>(p);

    // Check for overlong encodings and invalid code points
    if ((seq_len == 2 && cp < 0x80) ||
        (seq_len == 3 && cp < 0x800) ||
        (seq_len == 4 && cp < 0x10000) ||
        cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF)) {
        return k_replacement_char;
    }

    return cp;
}

std::vector<char32_t> utf8_to_codepoints(std::string_view utf8)
{
    std::vector<char32_t> result;
    result.reserve(utf8.size()); // Upper bound estimate

    const char* it = utf8.data();
    const char* end = it + utf8.size();

    while (it < end) {
        result.push_back(utf8_decode_one(it, end));
    }

    return result;
}

size_t codepoint_to_utf8(char32_t cp, char* out) noexcept
{
    auto* p = reinterpret_cast<unsigned char*>(out);

    if (cp < 0x80) {
        *p = static_cast<unsigned char>(cp);
        return 1;
    }
    else if (cp < 0x800) {
        p[0] = static_cast<unsigned char>(0xC0 | (cp >> 6));
        p[1] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
        return 2;
    }
    else if (cp < 0x10000) {
        // Reject surrogates
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            return 0;
        }
        p[0] = static_cast<unsigned char>(0xE0 | (cp >> 12));
        p[1] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
        p[2] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
        return 3;
    }
    else if (cp <= 0x10FFFF) {
        p[0] = static_cast<unsigned char>(0xF0 | (cp >> 18));
        p[1] = static_cast<unsigned char>(0x80 | ((cp >> 12) & 0x3F));
        p[2] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
        p[3] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
        return 4;
    }

    return 0; // Invalid code point
}

std::string codepoints_to_utf8(const std::vector<char32_t>& codepoints)
{
    std::string result;
    result.reserve(codepoints.size() * 2); // Rough estimate

    char buf[4];
    for (char32_t cp : codepoints) {
        size_t len = codepoint_to_utf8(cp, buf);
        if (len > 0) {
            result.append(buf, len);
        }
    }

    return result;
}

size_t utf8_length(std::string_view utf8) noexcept
{
    size_t count = 0;
    const char* it = utf8.data();
    const char* end = it + utf8.size();

    while (it < end) {
        (void)utf8_decode_one(it, end);
        ++count;
    }

    return count;
}

bool is_printable(char32_t cp) noexcept
{
    // Basic printable check (covers ASCII and common Unicode)
    if (cp < 0x20) {
        return false;  // Control characters
    }
    if (cp == 0x7F) {
        return false;  // DEL
    }
    if (cp >= 0x80 && cp < 0xA0) {
        return false;  // C1 control characters
    }
    if (cp > 0x10FFFF) {
        return false;  // Beyond Unicode
    }
    return true;
}

bool is_whitespace(char32_t cp) noexcept
{
    switch (cp) {
        case 0x0009: // Tab
        case 0x000A: // Line feed
        case 0x000B: // Vertical tab
        case 0x000C: // Form feed
        case 0x000D: // Carriage return
        case 0x0020: // Space
        case 0x0085: // Next line
        case 0x00A0: // Non-breaking space
        case 0x1680: // Ogham space mark
        case 0x2000: case 0x2001: case 0x2002: case 0x2003:
        case 0x2004: case 0x2005: case 0x2006: case 0x2007:
        case 0x2008: case 0x2009: case 0x200A: // Various spaces
        case 0x2028: // Line separator
        case 0x2029: // Paragraph separator
        case 0x202F: // Narrow no-break space
        case 0x205F: // Medium mathematical space
        case 0x3000: // Ideographic space
            return true;
        default:
            return false;
    }
}

bool is_digit(char32_t cp) noexcept
{
    return cp >= '0' && cp <= '9';
}

std::string_view trim(std::string_view str) noexcept
{
    // Trim ASCII whitespace from both ends
    const char* begin = str.data();
    const char* end = begin + str.size();

    while (begin < end && (*begin == ' ' || *begin == '\t' ||
                            *begin == '\n' || *begin == '\r')) {
        ++begin;
    }

    while (end > begin && (*(end - 1) == ' ' || *(end - 1) == '\t' ||
                            *(end - 1) == '\n' || *(end - 1) == '\r')) {
        --end;
    }

    return std::string_view(begin, static_cast<size_t>(end - begin));
}

std::vector<std::string_view> split(std::string_view str, char delim)
{
    std::vector<std::string_view> result;

    size_t start = 0;
    size_t end = str.find(delim);

    while (end != std::string_view::npos) {
        result.push_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(delim, start);
    }

    result.push_back(str.substr(start));
    return result;
}

} // namespace vnm::plot
