// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/cluster.hpp>

namespace hotkv {

uint16_t crc16(std::string_view data) {
    uint16_t crc = 0;
    for (unsigned char byte : data) {
        crc ^= static_cast<uint16_t>(byte) << 8;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t key_slot(std::string_view key) {
    constexpr uint16_t kClusterSlots = 16384;
    std::string_view hashed = key;
    std::size_t open = key.find('{');
    if (open != std::string_view::npos) {
        std::size_t close = key.find('}', open + 1);
        if (close != std::string_view::npos && close > open + 1) {
            hashed = key.substr(open + 1, close - open - 1);
        }
    }
    return static_cast<uint16_t>(crc16(hashed) % kClusterSlots);
}

} // namespace hotkv
