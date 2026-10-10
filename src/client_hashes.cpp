// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

bool Client::hset(std::string_view key, std::string_view field, std::string_view value) {
    return command({"HSET", std::string(key), std::string(field), std::string(value)}).as_int() != 0;
}

int64_t Client::hset(std::string_view key, const std::vector<std::pair<std::string, std::string>>& fields) {
    std::vector<std::string> args{"HSET", std::string(key)};
    for (const auto& [f, v] : fields) {
        args.push_back(f);
        args.push_back(v);
    }
    return command(std::move(args)).as_int();
}

std::optional<std::string> Client::hget(std::string_view key, std::string_view field) {
    return command({"HGET", std::string(key), std::string(field)}).as_optional_string();
}

std::vector<std::optional<std::string>> Client::hmget(std::string_view key, const std::vector<std::string>& fields) {
    std::vector<std::string> args{"HMGET", std::string(key)};
    args.insert(args.end(), fields.begin(), fields.end());
    Reply r = command(std::move(args));
    std::vector<std::optional<std::string>> out;
    out.reserve(r.as_array().size());
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_optional_string());
    }
    return out;
}

void Client::hmset(std::string_view key, const std::vector<std::pair<std::string, std::string>>& fields) {
    std::vector<std::string> args{"HMSET", std::string(key)};
    for (const auto& [f, v] : fields) {
        args.push_back(f);
        args.push_back(v);
    }
    command(std::move(args));
}

int64_t Client::hdel(std::string_view key, const std::vector<std::string>& fields) {
    std::vector<std::string> args{"HDEL", std::string(key)};
    args.insert(args.end(), fields.begin(), fields.end());
    return command(std::move(args)).as_int();
}

bool Client::hexists(std::string_view key, std::string_view field) {
    return command({"HEXISTS", std::string(key), std::string(field)}).as_int() != 0;
}

int64_t Client::hlen(std::string_view key) {
    return command({"HLEN", std::string(key)}).as_int();
}

std::vector<std::string> Client::hkeys(std::string_view key) {
    return command({"HKEYS", std::string(key)}).as_string_array();
}

std::vector<std::string> Client::hvals(std::string_view key) {
    return command({"HVALS", std::string(key)}).as_string_array();
}

std::vector<std::pair<std::string, std::string>> Client::hgetall(std::string_view key) {
    Reply r = command({"HGETALL", std::string(key)});
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& kv : r.as_pairs()) {
        out.emplace_back(kv.first, kv.second.is_nil() ? std::string() : kv.second.as_string());
    }
    return out;
}

int64_t Client::hincrby(std::string_view key, std::string_view field, int64_t delta) {
    return command({"HINCRBY", std::string(key), std::string(field), std::to_string(delta)}).as_int();
}

double Client::hincrbyfloat(std::string_view key, std::string_view field, double delta) {
    return command({"HINCRBYFLOAT", std::string(key), std::string(field), detail::format_finite(delta)}).as_double();
}

bool Client::hsetnx(std::string_view key, std::string_view field, std::string_view value) {
    return command({"HSETNX", std::string(key), std::string(field), std::string(value)}).as_int() != 0;
}

int64_t Client::hstrlen(std::string_view key, std::string_view field) {
    return command({"HSTRLEN", std::string(key), std::string(field)}).as_int();
}

std::vector<std::string> Client::hrandfield(std::string_view key, int64_t count) {
    return command({"HRANDFIELD", std::string(key), std::to_string(count)}).as_string_array();
}

std::vector<std::pair<std::string, std::string>> Client::hrandfield_withvalues(std::string_view key,
                                                                                 int64_t count) {
    Reply r = command({"HRANDFIELD", std::string(key), std::to_string(count), "WITHVALUES"});
    std::vector<std::pair<std::string, std::string>> out;
    // WITHVALUES switches from a flat array to an array of 2-element
    // sub-arrays under RESP3; as_pair_list() decodes either.
    for (auto& pair : r.as_pair_list()) {
        out.emplace_back(pair.first.as_string(), pair.second.as_string());
    }
    return out;
}

} // namespace hotkv
