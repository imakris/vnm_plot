#pragma once

// VNM Plot Library - SHA-256 Implementation
// Minimal, self-contained SHA-256 for cache validation.
// Based on public domain implementation.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace vnm::plot {

// -----------------------------------------------------------------------------
// SHA256 Hash
// -----------------------------------------------------------------------------

class Sha256
{
public:
    static constexpr size_t DIGEST_SIZE = 32;
    using Digest = std::array<uint8_t, DIGEST_SIZE>;

    Sha256() noexcept { reset(); }

    // Reset for reuse
    void reset() noexcept
    {
        m_state[0] = 0x6a09e667;
        m_state[1] = 0xbb67ae85;
        m_state[2] = 0x3c6ef372;
        m_state[3] = 0xa54ff53a;
        m_state[4] = 0x510e527f;
        m_state[5] = 0x9b05688c;
        m_state[6] = 0x1f83d9ab;
        m_state[7] = 0x5be0cd19;
        m_total_len = 0;
        m_buf_len = 0;
    }

    // Add data to hash
    void update(const void* data, size_t len) noexcept
    {
        const auto* bytes = static_cast<const uint8_t*>(data);

        // Fill buffer first
        if (m_buf_len > 0) {
            size_t to_copy = std::min(len, size_t(64) - m_buf_len);
            std::memcpy(m_buffer + m_buf_len, bytes, to_copy);
            m_buf_len += to_copy;
            bytes += to_copy;
            len -= to_copy;

            if (m_buf_len == 64) {
                transform(m_buffer);
                m_total_len += 64;
                m_buf_len = 0;
            }
        }

        // Process full blocks
        while (len >= 64) {
            transform(bytes);
            m_total_len += 64;
            bytes += 64;
            len -= 64;
        }

        // Store remaining bytes
        if (len > 0) {
            std::memcpy(m_buffer, bytes, len);
            m_buf_len = len;
        }
    }

    void update(std::string_view sv) noexcept
    {
        update(sv.data(), sv.size());
    }

    void update(const std::vector<uint8_t>& data) noexcept
    {
        update(data.data(), data.size());
    }

    // Finalize and get digest
    [[nodiscard]] Digest finalize() noexcept
    {
        // Calculate total length in bits
        uint64_t total_bits = (m_total_len + m_buf_len) * 8;

        // Pad message
        m_buffer[m_buf_len++] = 0x80;

        if (m_buf_len > 56) {
            while (m_buf_len < 64) {
                m_buffer[m_buf_len++] = 0;
            }
            transform(m_buffer);
            m_buf_len = 0;
        }

        while (m_buf_len < 56) {
            m_buffer[m_buf_len++] = 0;
        }

        // Append length (big endian)
        for (int i = 7; i >= 0; --i) {
            m_buffer[m_buf_len++] = static_cast<uint8_t>(total_bits >> (i * 8));
        }

        transform(m_buffer);

        // Convert state to digest (big endian)
        Digest digest;
        for (int i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>(m_state[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(m_state[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(m_state[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i]);
        }

        reset();
        return digest;
    }

    // Convenience: hash data in one call
    [[nodiscard]] static Digest hash(const void* data, size_t len)
    {
        Sha256 ctx;
        ctx.update(data, len);
        return ctx.finalize();
    }

    [[nodiscard]] static Digest hash(std::string_view sv)
    {
        return hash(sv.data(), sv.size());
    }

    [[nodiscard]] static Digest hash(const std::vector<uint8_t>& data)
    {
        return hash(data.data(), data.size());
    }

    // Convert digest to hex string
    [[nodiscard]] static std::string to_hex(const Digest& digest)
    {
        static const char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(DIGEST_SIZE * 2);
        for (uint8_t b : digest) {
            result.push_back(hex[b >> 4]);
            result.push_back(hex[b & 0x0f]);
        }
        return result;
    }

private:
    void transform(const uint8_t* block) noexcept
    {
        static constexpr uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
            0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        uint32_t w[64];

        // Prepare message schedule
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block[i * 4 + 0]) << 24) |
                   (uint32_t(block[i * 4 + 1]) << 16) |
                   (uint32_t(block[i * 4 + 2]) << 8) |
                   (uint32_t(block[i * 4 + 3]));
        }

        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        // Initialize working variables
        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];

        // Main loop
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        // Add to state
        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    static constexpr uint32_t rotr(uint32_t x, int n) noexcept
    {
        return (x >> n) | (x << (32 - n));
    }

    uint32_t m_state[8];
    uint64_t m_total_len;
    uint8_t m_buffer[64];
    size_t m_buf_len;
};

} // namespace vnm::plot
