// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

namespace {

std::vector<std::string> with_keys(const char* cmd, const std::vector<std::string>& keys) {
    std::vector<std::string> args{cmd};
    args.insert(args.end(), keys.begin(), keys.end());
    return args;
}

} // namespace

int64_t Client::sadd(std::string_view key, const std::vector<std::string>& members) {
    std::vector<std::string> args{"SADD", std::string(key)};
    args.insert(args.end(), members.begin(), members.end());
    return command(std::move(args)).as_int();
}

int64_t Client::srem(std::string_view key, const std::vector<std::string>& members) {
    std::vector<std::string> args{"SREM", std::string(key)};
    args.insert(args.end(), members.begin(), members.end());
    return command(std::move(args)).as_int();
}

std::vector<std::string> Client::smembers(std::string_view key) {
    return command({"SMEMBERS", std::string(key)}).as_string_array();
}

bool Client::sismember(std::string_view key, std::string_view member) {
    return command({"SISMEMBER", std::string(key), std::string(member)}).as_int() != 0;
}

std::vector<bool> Client::smismember(std::string_view key, const std::vector<std::string>& members) {
    std::vector<std::string> args{"SMISMEMBER", std::string(key)};
    args.insert(args.end(), members.begin(), members.end());
    Reply r = command(std::move(args));
    std::vector<bool> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_int() != 0);
    }
    return out;
}

int64_t Client::scard(std::string_view key) {
    return command({"SCARD", std::string(key)}).as_int();
}

std::optional<std::string> Client::spop(std::string_view key) {
    return command({"SPOP", std::string(key)}).as_optional_string();
}

std::vector<std::string> Client::spop(std::string_view key, int64_t count) {
    Reply r = command({"SPOP", std::string(key), std::to_string(count)});
    return r.is_nil() ? std::vector<std::string>{} : r.as_string_array();
}

std::optional<std::string> Client::srandmember(std::string_view key) {
    return command({"SRANDMEMBER", std::string(key)}).as_optional_string();
}

std::vector<std::string> Client::srandmember(std::string_view key, int64_t count) {
    Reply r = command({"SRANDMEMBER", std::string(key), std::to_string(count)});
    return r.is_nil() ? std::vector<std::string>{} : r.as_string_array();
}

std::vector<std::string> Client::sinter(const std::vector<std::string>& keys) {
    return command(with_keys("SINTER", keys)).as_string_array();
}

int64_t Client::sinterstore(std::string_view dst, const std::vector<std::string>& keys) {
    std::vector<std::string> args{"SINTERSTORE", std::string(dst)};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

int64_t Client::sintercard(const std::vector<std::string>& keys, int64_t limit) {
    std::vector<std::string> args{"SINTERCARD", std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    if (limit > 0) {
        args.emplace_back("LIMIT");
        args.push_back(std::to_string(limit));
    }
    return command(std::move(args)).as_int();
}

std::vector<std::string> Client::sunion(const std::vector<std::string>& keys) {
    return command(with_keys("SUNION", keys)).as_string_array();
}

int64_t Client::sunionstore(std::string_view dst, const std::vector<std::string>& keys) {
    std::vector<std::string> args{"SUNIONSTORE", std::string(dst)};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

std::vector<std::string> Client::sdiff(const std::vector<std::string>& keys) {
    return command(with_keys("SDIFF", keys)).as_string_array();
}

int64_t Client::sdiffstore(std::string_view dst, const std::vector<std::string>& keys) {
    std::vector<std::string> args{"SDIFFSTORE", std::string(dst)};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

bool Client::smove(std::string_view src, std::string_view dst, std::string_view member) {
    return command({"SMOVE", std::string(src), std::string(dst), std::string(member)}).as_int() != 0;
}

} // namespace hotkv
