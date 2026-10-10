// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

namespace hotkv {

// --- TagCommands ---

int64_t TagCommands::set(std::string_view key, const std::vector<std::string>& tags) {
    std::vector<std::string> args{"TAG.SET", std::string(key)};
    args.insert(args.end(), tags.begin(), tags.end());
    return client_.command(std::move(args)).as_int();
}

int64_t TagCommands::rem(std::string_view key, const std::vector<std::string>& tags) {
    std::vector<std::string> args{"TAG.REM", std::string(key)};
    args.insert(args.end(), tags.begin(), tags.end());
    return client_.command(std::move(args)).as_int();
}

std::vector<std::string> TagCommands::get(std::string_view key) {
    return client_.command({"TAG.GET", std::string(key)}).as_string_array();
}

std::vector<std::string> TagCommands::members(std::string_view tag) {
    return client_.command({"TAG.MEMBERS", std::string(tag)}).as_string_array();
}

std::vector<std::string> TagCommands::inter(const std::vector<std::string>& tags) {
    std::vector<std::string> args{"TAG.INTER"};
    args.insert(args.end(), tags.begin(), tags.end());
    return client_.command(std::move(args)).as_string_array();
}

int64_t TagCommands::count(std::string_view key) {
    return client_.command({"TAG.COUNT", std::string(key)}).as_int();
}

bool TagCommands::exists(std::string_view key, std::string_view tag) {
    return client_.command({"TAG.EXISTS", std::string(key), std::string(tag)}).as_int() != 0;
}

// --- HotKeysCommands ---

std::vector<std::pair<std::string, int64_t>> HotKeysCommands::top(int64_t count) {
    Reply r = client_.command({"HOTKEYS", "TOP", std::to_string(count)});
    std::vector<std::pair<std::string, int64_t>> out;
    for (const Reply& item : r.as_array()) {
        const Reply::Array& pair = item.as_array();
        out.emplace_back(pair.at(0).as_string(), pair.at(1).as_int());
    }
    return out;
}

void HotKeysCommands::reset() {
    client_.command({"HOTKEYS", "RESET"});
}

// --- GcraCommands ---

GcraResult GcraCommands::limit(std::string_view key, int64_t period_ms, int64_t burst, int64_t count) {
    Reply r = client_.command(
        {"GCRA.LIMIT", std::string(key), std::to_string(period_ms), std::to_string(burst), std::to_string(count)});
    const Reply::Array& arr = r.as_array();
    GcraResult res;
    if (arr.size() >= 5) {
        res.denied = arr[0].as_int() != 0;
        res.limit = arr[1].as_int();
        res.remaining = arr[2].as_int();
        res.retry_after_ms = arr[3].as_int();
        res.reset_after_ms = arr[4].as_int();
    }
    return res;
}

int64_t GcraCommands::reset(const std::vector<std::string>& keys) {
    std::vector<std::string> args{"GCRA.RESET"};
    args.insert(args.end(), keys.begin(), keys.end());
    return client_.command(std::move(args)).as_int();
}

} // namespace hotkv
