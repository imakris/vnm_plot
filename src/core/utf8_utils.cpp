#include "utf8_utils.h"

namespace vnm::plot {

namespace {

constexpr char32_t k_replacement_char = 0xFFFD;

// Check if byte is a UTF-8 continuation byte (10xxxxxx)
constexpr bool is_continuation(unsigned char c) noexcept
{
    return (c & 0xC0) == 0x80;
}

// Decode a single UTF-8 code point from a string.
// Returns the code point and advances the iterator.
// Returns 0xFFFD (replacement character) on invalid sequences.
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

// Convert a single Unicode code point to UTF-8.
// Returns the number of bytes written (1-4), or 0 on invalid code point.
// Output buffer must have at least 4 bytes.
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

} // anonymous namespace

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

} // namespace vnm::plot
