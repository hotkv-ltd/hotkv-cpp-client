// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/scan.hpp>

#include <hotkv/detail/numfmt.hpp>
#include <hotkv/error.hpp>

namespace hotkv {

Scanner::Scanner(std::function<Reply(const std::string&)> issue, int stride)
    : issue_(std::move(issue)), stride_(stride) {}

bool Scanner::advance_if_needed() {
    while (idx_ + static_cast<std::size_t>(stride_) > page_.size()) {
        if (finished_) {
            return false;
        }
        Reply reply = issue_(cursor_);
        const Reply::Array& top = reply.as_array();
        if (top.size() != 2) {
            throw ProtocolError("hotkv: malformed SCAN reply (expected [cursor, items])");
        }
        cursor_ = top[0].as_string();
        page_ = top[1].as_string_array();
        idx_ = 0;
        started_ = true;
        if (cursor_ == "0") {
            finished_ = true;
        }
        if (page_.empty() && finished_) {
            return false;
        }
    }
    return true;
}

bool Scanner::next(std::string& value) {
    if (stride_ != 1) {
        throw Error("hotkv: this Scanner yields pairs; call next(first, second) or next_scored()");
    }
    if (!advance_if_needed()) {
        return false;
    }
    value = page_[idx_++];
    return true;
}

bool Scanner::next(std::string& first, std::string& second) {
    if (stride_ != 2) {
        throw Error("hotkv: this Scanner yields single values; call next(value)");
    }
    if (!advance_if_needed()) {
        return false;
    }
    first = page_[idx_++];
    second = page_[idx_++];
    return true;
}

bool Scanner::next_scored(std::string& member, double& score) {
    std::string score_str;
    if (!next(member, score_str)) {
        return false;
    }
    if (!detail::parse_double(score_str, score)) {
        throw ProtocolError("hotkv: malformed sorted set score: \"" + score_str + "\"");
    }
    return true;
}

} // namespace hotkv
