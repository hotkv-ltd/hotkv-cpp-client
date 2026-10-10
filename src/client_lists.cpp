// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

namespace {

int64_t push(Client& client, const char* cmd, std::string_view key, const std::vector<std::string>& values) {
    std::vector<std::string> args{cmd, std::string(key)};
    args.insert(args.end(), values.begin(), values.end());
    return client.command(std::move(args)).as_int();
}

} // namespace

int64_t Client::lpush(std::string_view key, const std::vector<std::string>& values) {
    return push(*this, "LPUSH", key, values);
}

int64_t Client::rpush(std::string_view key, const std::vector<std::string>& values) {
    return push(*this, "RPUSH", key, values);
}

int64_t Client::lpushx(std::string_view key, const std::vector<std::string>& values) {
    return push(*this, "LPUSHX", key, values);
}

int64_t Client::rpushx(std::string_view key, const std::vector<std::string>& values) {
    return push(*this, "RPUSHX", key, values);
}

std::optional<std::string> Client::lpop(std::string_view key) {
    return command({"LPOP", std::string(key)}).as_optional_string();
}

std::optional<std::string> Client::rpop(std::string_view key) {
    return command({"RPOP", std::string(key)}).as_optional_string();
}

std::vector<std::string> Client::lpop(std::string_view key, int64_t count) {
    Reply r = command({"LPOP", std::string(key), std::to_string(count)});
    return r.is_nil() ? std::vector<std::string>{} : r.as_string_array();
}

std::vector<std::string> Client::rpop(std::string_view key, int64_t count) {
    Reply r = command({"RPOP", std::string(key), std::to_string(count)});
    return r.is_nil() ? std::vector<std::string>{} : r.as_string_array();
}

int64_t Client::llen(std::string_view key) {
    return command({"LLEN", std::string(key)}).as_int();
}

std::vector<std::string> Client::lrange(std::string_view key, int64_t start, int64_t stop) {
    return command({"LRANGE", std::string(key), std::to_string(start), std::to_string(stop)}).as_string_array();
}

void Client::ltrim(std::string_view key, int64_t start, int64_t stop) {
    command({"LTRIM", std::string(key), std::to_string(start), std::to_string(stop)});
}

std::optional<std::string> Client::lindex(std::string_view key, int64_t index) {
    return command({"LINDEX", std::string(key), std::to_string(index)}).as_optional_string();
}

bool Client::lset(std::string_view key, int64_t index, std::string_view value) {
    command({"LSET", std::string(key), std::to_string(index), std::string(value)});
    return true;
}

int64_t Client::lrem(std::string_view key, int64_t count, std::string_view value) {
    return command({"LREM", std::string(key), std::to_string(count), std::string(value)}).as_int();
}

int64_t Client::linsert_before(std::string_view key, std::string_view pivot, std::string_view value) {
    return command({"LINSERT", std::string(key), "BEFORE", std::string(pivot), std::string(value)}).as_int();
}

int64_t Client::linsert_after(std::string_view key, std::string_view pivot, std::string_view value) {
    return command({"LINSERT", std::string(key), "AFTER", std::string(pivot), std::string(value)}).as_int();
}

std::optional<std::string> Client::lmove(std::string_view src, std::string_view dst, bool src_left, bool dst_left) {
    return command({"LMOVE", std::string(src), std::string(dst), src_left ? "LEFT" : "RIGHT",
                     dst_left ? "LEFT" : "RIGHT"})
        .as_optional_string();
}

std::optional<std::string> Client::rpoplpush(std::string_view src, std::string_view dst) {
    return command({"RPOPLPUSH", std::string(src), std::string(dst)}).as_optional_string();
}

} // namespace hotkv
