// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "sha1.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace hotkv::detail {

namespace {

inline uint32_t rotl(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

// Straightforward FIPS 180-4 SHA-1, one 64-byte block at a time.
class Sha1 {
public:
    Sha1() { state_ = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}; }

    void update(const uint8_t* data, size_t len) {
        total_bits_ += static_cast<uint64_t>(len) * 8;
        while (len > 0) {
            size_t take = std::min(len, sizeof(buffer_) - buffer_len_);
            std::memcpy(buffer_ + buffer_len_, data, take);
            buffer_len_ += take;
            data += take;
            len -= take;
            if (buffer_len_ == sizeof(buffer_)) {
                process_block(buffer_);
                buffer_len_ = 0;
            }
        }
    }

    std::array<uint8_t, 20> finish() {
        const uint64_t bit_length = total_bits_;

        append_byte(0x80);
        while (buffer_len_ != 56) {
            append_byte(0x00);
        }
        for (int i = 7; i >= 0; --i) {
            append_byte(static_cast<uint8_t>(bit_length >> (8 * i)));
        }

        std::array<uint8_t, 20> out{};
        for (int i = 0; i < 5; ++i) {
            out[4 * i + 0] = static_cast<uint8_t>(state_[i] >> 24);
            out[4 * i + 1] = static_cast<uint8_t>(state_[i] >> 16);
            out[4 * i + 2] = static_cast<uint8_t>(state_[i] >> 8);
            out[4 * i + 3] = static_cast<uint8_t>(state_[i]);
        }
        return out;
    }

private:
    void append_byte(uint8_t b) {
        buffer_[buffer_len_++] = b;
        if (buffer_len_ == sizeof(buffer_)) {
            process_block(buffer_);
            buffer_len_ = 0;
        }
    }

    void process_block(const uint8_t block[64]) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) | (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
                   (static_cast<uint32_t>(block[4 * i + 2]) << 8) | static_cast<uint32_t>(block[4 * i + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::array<uint32_t, 5> state_{};
    uint8_t buffer_[64] = {};
    size_t buffer_len_ = 0;
    uint64_t total_bits_ = 0;
};

} // namespace

std::string sha1_hex(std::string_view data) {
    Sha1 hasher;
    hasher.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    std::array<uint8_t, 20> digest = hasher.finish();

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (uint8_t byte : digest) {
        out.push_back(hex[byte >> 4]);
        out.push_back(hex[byte & 0x0f]);
    }
    return out;
}

} // namespace hotkv::detail
