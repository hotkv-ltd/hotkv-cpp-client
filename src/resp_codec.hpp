// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include "socket_io.hpp"

#include <hotkv/reply.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace hotkv::detail {

// Encodes args as a RESP array-of-bulk-strings command (the wire form
// every client-to-server command uses, independent of the negotiated
// reply protocol version).
std::string encode_command(const std::vector<std::string>& args);

// Buffered RESP2/RESP3 reply reader over an IoStream. Reads one full
// reply per read_reply() call, recursing into nested container types.
class RespReader {
public:
    explicit RespReader(IoStream& stream) : stream_(stream) {}

    Reply read_reply();

private:
    void ensure(std::size_t n);
    uint8_t get_byte();
    std::string read_line();
    std::string read_exact(std::size_t n);
    Reply parse_value();
    Reply parse_value_inner();

    IoStream& stream_;
    int depth_ = 0;
    std::string buf_;
    std::size_t pos_ = 0;
};

} // namespace hotkv::detail
