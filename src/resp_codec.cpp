// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "resp_codec.hpp"

#include <hotkv/detail/numfmt.hpp>
#include <hotkv/error.hpp>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace hotkv::detail {

std::string encode_command(const std::vector<std::string>& args) {
    std::string out;
    std::size_t estimate = 16;
    for (const auto& a : args) {
        estimate += a.size() + 16;
    }
    out.reserve(estimate);

    out += '*';
    out += std::to_string(args.size());
    out += "\r\n";
    for (const auto& a : args) {
        out += '$';
        out += std::to_string(a.size());
        out += "\r\n";
        out += a;
        out += "\r\n";
    }
    return out;
}

namespace {

int64_t parse_i64(const std::string& text) {
    int64_t value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size()) {
        throw ProtocolError("hotkv: malformed RESP integer: \"" + text + "\"");
    }
    return value;
}

double parse_double_token(const std::string& text) {
    double value = 0.0;
    if (!parse_double(text, value)) {
        throw ProtocolError("hotkv: malformed RESP double: \"" + text + "\"");
    }
    return value;
}

} // namespace

void RespReader::ensure(std::size_t n) {
    while (buf_.size() - pos_ < n) {
        if (pos_ > 0) {
            buf_.erase(0, pos_);
            pos_ = 0;
        }
        char chunk[4096];
        std::size_t got = stream_.read_some(chunk, sizeof(chunk));
        if (got == 0) {
            throw ConnectionError("hotkv: connection closed by peer while reading a reply");
        }
        buf_.append(chunk, got);
    }
}

uint8_t RespReader::get_byte() {
    ensure(1);
    return static_cast<uint8_t>(buf_[pos_++]);
}

std::string RespReader::read_line() {
    for (;;) {
        std::size_t idx = buf_.find("\r\n", pos_);
        if (idx != std::string::npos) {
            std::string line = buf_.substr(pos_, idx - pos_);
            pos_ = idx + 2;
            return line;
        }
        if (pos_ > 0) {
            buf_.erase(0, pos_);
            pos_ = 0;
        }
        char chunk[4096];
        std::size_t got = stream_.read_some(chunk, sizeof(chunk));
        if (got == 0) {
            throw ConnectionError("hotkv: connection closed by peer while reading a reply");
        }
        buf_.append(chunk, got);
    }
}

std::string RespReader::read_exact(std::size_t n) {
    std::string out;
    out.resize(n);
    std::size_t avail = buf_.size() - pos_;
    std::size_t take = std::min(avail, n);
    if (take > 0) {
        std::memcpy(out.data(), buf_.data() + pos_, take);
        pos_ += take;
    }
    std::size_t got_total = take;
    while (got_total < n) {
        std::size_t got = stream_.read_some(out.data() + got_total, n - got_total);
        if (got == 0) {
            throw ConnectionError("hotkv: connection closed by peer while reading a bulk payload");
        }
        got_total += got;
    }
    ensure(2);
    if (buf_[pos_] != '\r' || buf_[pos_ + 1] != '\n') {
        throw ProtocolError("hotkv: expected CRLF after bulk payload");
    }
    pos_ += 2;
    return out;
}

namespace {

// A reply that claims more than this is not from a HotKV server (the
// server's own maximum bulk length is 512 MiB); refusing it stops a
// hostile or corrupt peer from making the client allocate without bound.
constexpr int64_t kMaxBulkBytes = int64_t{512} * 1024 * 1024 + 1024;
constexpr int64_t kMaxAggregateLength = int64_t{1} << 31;
constexpr int kMaxNesting = 256;

std::size_t checked_length(int64_t len, int64_t limit, const char* what) {
    if (len > limit) {
        throw ProtocolError(std::string("hotkv: RESP ") + what + " length " + std::to_string(len) +
                            " exceeds the sanity limit");
    }
    return static_cast<std::size_t>(len);
}

// Reserve only a bounded amount up front; a lying length then fails when
// the data does not arrive, not with an allocation failure.
std::size_t reserve_hint(int64_t len) {
    return static_cast<std::size_t>(std::min<int64_t>(len, 4096));
}

} // namespace

Reply RespReader::parse_value() {
    if (++depth_ > kMaxNesting) {
        depth_ = 0;
        throw ProtocolError("hotkv: RESP reply nested too deeply");
    }
    struct DepthGuard {
        int& d;
        ~DepthGuard() {
            if (d > 0) {
                --d;
            }
        }
    } guard{depth_};
    return parse_value_inner();
}

Reply RespReader::parse_value_inner() {
    uint8_t type_byte = get_byte();
    switch (type_byte) {
        case '+':
            return Reply::make_simple_string(read_line());
        case '-':
            return Reply::make_error(read_line());
        case ':':
            return Reply::make_integer(parse_i64(read_line()));
        case '$': {
            std::string len_str = read_line();
            int64_t len = parse_i64(len_str);
            if (len < 0) {
                return Reply::make_null();
            }
            return Reply::make_bulk_string(read_exact(checked_length(len, kMaxBulkBytes, "bulk")));
        }
        case '_':
            read_line();
            return Reply::make_null();
        case '#': {
            std::string line = read_line();
            if (line == "t") {
                return Reply::make_boolean(true);
            }
            if (line == "f") {
                return Reply::make_boolean(false);
            }
            throw ProtocolError("hotkv: malformed RESP boolean: \"" + line + "\"");
        }
        case ',':
            return Reply::make_double(parse_double_token(read_line()));
        case '(':
            return Reply::make_big_number(read_line());
        case '!': {
            std::string len_str = read_line();
            int64_t len = parse_i64(len_str);
            if (len < 0) {
                return Reply::make_null();
            }
            return Reply::make_error(read_exact(checked_length(len, kMaxBulkBytes, "bulk")));
        }
        case '=': {
            std::string len_str = read_line();
            int64_t len = parse_i64(len_str);
            if (len < 0) {
                return Reply::make_null();
            }
            std::string payload = read_exact(checked_length(len, kMaxBulkBytes, "bulk"));
            // Wire form: "<3-char format>:<text>".
            if (payload.size() >= 4 && payload[3] == ':') {
                return Reply::make_verbatim(payload.substr(0, 3), payload.substr(4));
            }
            return Reply::make_verbatim("txt", payload);
        }
        case '*': {
            std::string len_str = read_line();
            int64_t len = parse_i64(len_str);
            if (len < 0) {
                return Reply::make_null();
            }
            Reply::Array elements;
            checked_length(len, kMaxAggregateLength, "aggregate");
            elements.reserve(reserve_hint(len));
            for (int64_t i = 0; i < len; ++i) {
                elements.push_back(parse_value());
            }
            return Reply::make_array(std::move(elements));
        }
        case '~': {
            std::string len_str = read_line();
            int64_t len = parse_i64(len_str);
            if (len < 0) {
                return Reply::make_null();
            }
            Reply::Array elements;
            checked_length(len, kMaxAggregateLength, "aggregate");
            elements.reserve(reserve_hint(len));
            for (int64_t i = 0; i < len; ++i) {
                elements.push_back(parse_value());
            }
            return Reply::make_set(std::move(elements));
        }
        case '>': {
            std::string len_str = read_line();
            int64_t len = parse_i64(len_str);
            if (len < 0) {
                return Reply::make_null();
            }
            Reply::Array elements;
            checked_length(len, kMaxAggregateLength, "aggregate");
            elements.reserve(reserve_hint(len));
            for (int64_t i = 0; i < len; ++i) {
                elements.push_back(parse_value());
            }
            return Reply::make_push(std::move(elements));
        }
        case '%': {
            std::string len_str = read_line();
            int64_t len = parse_i64(len_str);
            if (len < 0) {
                return Reply::make_null();
            }
            Reply::MapEntries entries;
            checked_length(len, kMaxAggregateLength, "map");
            entries.reserve(reserve_hint(len));
            for (int64_t i = 0; i < len; ++i) {
                Reply key = parse_value();
                Reply value = parse_value();
                entries.emplace_back(std::move(key), std::move(value));
            }
            return Reply::make_map(std::move(entries));
        }
        case '|': {
            std::string len_str = read_line();
            int64_t len = parse_i64(len_str);
            Reply::MapEntries attrs;
            if (len > 0) {
                checked_length(len, kMaxAggregateLength, "attribute");
                attrs.reserve(reserve_hint(len));
                for (int64_t i = 0; i < len; ++i) {
                    Reply key = parse_value();
                    Reply value = parse_value();
                    attrs.emplace_back(std::move(key), std::move(value));
                }
            }
            Reply real = parse_value();
            real.set_attributes(std::move(attrs));
            return real;
        }
        default:
            throw ProtocolError(std::string("hotkv: unknown RESP type byte: '") +
                                 static_cast<char>(type_byte) + "'");
    }
}

Reply RespReader::read_reply() {
    return parse_value();
}

} // namespace hotkv::detail
