// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include "socket_io.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace hotkv::testutil {

// A fake IoStream that replays a fixed sequence of read chunks (to
// exercise RespReader against a reply that arrives split across many
// small reads, the way a real socket would deliver it) and records
// everything written to it.
class FakeStream : public hotkv::detail::IoStream {
public:
    explicit FakeStream(std::vector<std::string> read_chunks) : chunks_(std::move(read_chunks)) {}

    std::size_t read_some(char* buf, std::size_t len) override {
        if (chunk_index_ >= chunks_.size()) {
            return 0;
        }
        const std::string& chunk = chunks_[chunk_index_];
        std::size_t take = std::min(len, chunk.size() - offset_in_chunk_);
        std::memcpy(buf, chunk.data() + offset_in_chunk_, take);
        offset_in_chunk_ += take;
        if (offset_in_chunk_ >= chunk.size()) {
            ++chunk_index_;
            offset_in_chunk_ = 0;
        }
        return take;
    }

    void write_all(const char* buf, std::size_t len) override { written_.append(buf, len); }
    void close() noexcept override { closed_ = true; }
    void set_timeouts(std::chrono::milliseconds, std::chrono::milliseconds) override {}

    const std::string& written() const { return written_; }
    bool closed() const { return closed_; }

private:
    std::vector<std::string> chunks_;
    std::size_t chunk_index_ = 0;
    std::size_t offset_in_chunk_ = 0;
    std::string written_;
    bool closed_ = false;
};

// Splits `data` into chunks of at most `chunk_size` bytes, to feed a
// FakeStream and simulate a slow/fragmented socket.
inline std::vector<std::string> split_into_chunks(const std::string& data, std::size_t chunk_size) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < data.size(); i += chunk_size) {
        out.push_back(data.substr(i, chunk_size));
    }
    if (out.empty()) {
        out.emplace_back();
    }
    return out;
}

} // namespace hotkv::testutil
