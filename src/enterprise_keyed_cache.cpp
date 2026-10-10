// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

#include "pairs_util.hpp"
#include "usage_util.hpp"

namespace hotkv {

namespace {
std::string wire(const std::string& prefix, const char* suffix) {
    return prefix + "." + suffix;
}
} // namespace

void KeyedCacheCommands::create(std::string_view ns, std::optional<int64_t> max_entries,
                                 std::optional<int64_t> ttl_seconds) {
    std::vector<std::string> args{wire(prefix_, "CREATE"), std::string(ns)};
    if (max_entries.has_value()) {
        args.emplace_back("MAX_ENTRIES");
        args.push_back(std::to_string(*max_entries));
    }
    if (ttl_seconds.has_value()) {
        args.emplace_back("TTL");
        args.push_back(std::to_string(*ttl_seconds));
    }
    client_.command(std::move(args));
}

void KeyedCacheCommands::set(std::string_view ns, std::string_view key, std::string_view value,
                              std::optional<int64_t> ttl_seconds, std::string_view meta_json,
                              const std::optional<Usage>& usage) {
    std::vector<std::string> args{wire(prefix_, "SET"), std::string(ns), std::string(key), std::string(value)};
    if (ttl_seconds.has_value()) {
        args.emplace_back("TTL");
        args.push_back(std::to_string(*ttl_seconds));
    }
    if (!meta_json.empty()) {
        args.emplace_back("META");
        args.emplace_back(meta_json);
    }
    detail::append_usage(args, usage);
    client_.command(std::move(args));
}

std::optional<std::string> KeyedCacheCommands::get(std::string_view ns, std::string_view key) {
    return client_.command({wire(prefix_, "GET"), std::string(ns), std::string(key)}).as_optional_string();
}

bool KeyedCacheCommands::del(std::string_view ns, std::string_view key) {
    return client_.command({wire(prefix_, "DEL"), std::string(ns), std::string(key)}).as_int() != 0;
}

KeyedCacheStats KeyedCacheCommands::stats(std::string_view ns) {
    auto pairs = client_.command({wire(prefix_, "STATS"), std::string(ns)}).as_pairs();
    KeyedCacheStats s;
    s.entries = detail::pairs_int(pairs, "entries");
    s.hits = detail::pairs_int(pairs, "hits");
    s.misses = detail::pairs_int(pairs, "misses");
    s.evictions = detail::pairs_int(pairs, "evictions");
    s.memory_bytes = detail::pairs_int(pairs, "memory_bytes");
    s.usage.requests = detail::pairs_int(pairs, "usage_saved_requests");
    s.usage.input_tokens = detail::pairs_int(pairs, "usage_saved_input_tokens");
    s.usage.output_tokens = detail::pairs_int(pairs, "usage_saved_output_tokens");
    s.usage.cache_read_tokens = detail::pairs_int(pairs, "usage_saved_cache_read_tokens");
    s.usage.cache_write_tokens = detail::pairs_int(pairs, "usage_saved_cache_write_tokens");
    s.usage.cost_usd = detail::pairs_double(pairs, "usage_saved_cost_usd");
    return s;
}

int64_t KeyedCacheCommands::flush(std::string_view ns) {
    return client_.command({wire(prefix_, "FLUSH"), std::string(ns)}).as_int();
}

bool KeyedCacheCommands::drop(std::string_view ns) {
    return client_.command({wire(prefix_, "DROP"), std::string(ns)}).as_int() != 0;
}

std::vector<std::string> KeyedCacheCommands::list() {
    return client_.command({wire(prefix_, "LIST")}).as_string_array();
}

} // namespace hotkv
