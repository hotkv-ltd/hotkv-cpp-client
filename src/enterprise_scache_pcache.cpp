// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

#include "pairs_util.hpp"
#include "usage_util.hpp"

namespace hotkv {

// --- SCacheCommands ---

void SCacheCommands::create(std::string_view ns, int64_t dims, const SCacheCreateOptions& opts) {
    std::vector<std::string> args{"SCACHE.CREATE", std::string(ns), std::to_string(dims)};
    if (!opts.metric.empty()) {
        args.emplace_back("METRIC");
        args.push_back(opts.metric);
    }
    if (opts.threshold.has_value()) {
        args.emplace_back("THRESHOLD");
        args.push_back(detail::format_finite(*opts.threshold));
    }
    if (opts.ttl_seconds.has_value()) {
        args.emplace_back("TTL");
        args.push_back(std::to_string(*opts.ttl_seconds));
    }
    client_.command(std::move(args));
}

void SCacheCommands::set(std::string_view ns, std::string_view query_text, std::string_view response,
                          const std::vector<double>& vector, std::optional<int64_t> ttl_seconds,
                          const std::optional<Usage>& usage) {
    std::vector<std::string> args{"SCACHE.SET", std::string(ns), std::string(query_text), std::string(response)};
    for (double v : vector) {
        args.push_back(detail::format_finite(v));
    }
    if (ttl_seconds.has_value()) {
        args.emplace_back("TTL");
        args.push_back(std::to_string(*ttl_seconds));
    }
    detail::append_usage(args, usage);
    client_.command(std::move(args));
}

std::optional<SCacheHit> SCacheCommands::get(std::string_view ns, const std::vector<double>& vector,
                                              std::optional<double> threshold_override) {
    std::vector<std::string> args{"SCACHE.GET", std::string(ns)};
    for (double v : vector) {
        args.push_back(detail::format_finite(v));
    }
    if (threshold_override.has_value()) {
        args.emplace_back("THRESHOLD");
        args.push_back(detail::format_finite(*threshold_override));
    }
    Reply r = client_.command(std::move(args));
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    SCacheHit hit;
    hit.response = arr.at(0).as_string();
    hit.similarity = arr.at(1).as_double();
    return hit;
}

SCacheStats SCacheCommands::stats(std::string_view ns) {
    auto pairs = client_.command({"SCACHE.STATS", std::string(ns)}).as_pairs();
    SCacheStats s;
    s.entries = detail::pairs_int(pairs, "entries");
    s.hits = detail::pairs_int(pairs, "hits");
    s.misses = detail::pairs_int(pairs, "misses");
    s.hit_rate = detail::pairs_double(pairs, "hit_rate");
    s.p50_us = detail::pairs_int(pairs, "p50_us");
    s.usage.requests = detail::pairs_int(pairs, "usage_saved_requests");
    s.usage.input_tokens = detail::pairs_int(pairs, "usage_saved_input_tokens");
    s.usage.output_tokens = detail::pairs_int(pairs, "usage_saved_output_tokens");
    s.usage.cache_read_tokens = detail::pairs_int(pairs, "usage_saved_cache_read_tokens");
    s.usage.cache_write_tokens = detail::pairs_int(pairs, "usage_saved_cache_write_tokens");
    s.usage.cost_usd = detail::pairs_double(pairs, "usage_saved_cost_usd");
    return s;
}

bool SCacheCommands::flush(std::string_view ns) {
    return client_.command({"SCACHE.FLUSH", std::string(ns)}).as_int() != 0;
}

bool SCacheCommands::drop(std::string_view ns) {
    return client_.command({"SCACHE.DROP", std::string(ns)}).as_int() != 0;
}

std::vector<std::string> SCacheCommands::list() {
    return client_.command({"SCACHE.LIST"}).as_string_array();
}

void SCacheCommands::set_threshold(std::string_view ns, double value) {
    client_.command({"SCACHE.THRESHOLD", std::string(ns), detail::format_finite(value)});
}

// --- PCacheCommands ---

void PCacheCommands::create(std::string_view ns, const PCacheCreateOptions& opts) {
    std::vector<std::string> args{"PCACHE.CREATE", std::string(ns)};
    if (opts.max_entries.has_value()) {
        args.emplace_back("MAX_ENTRIES");
        args.push_back(std::to_string(*opts.max_entries));
    }
    if (opts.ttl_seconds.has_value()) {
        args.emplace_back("TTL");
        args.push_back(std::to_string(*opts.ttl_seconds));
    }
    if (opts.cost_per_hit_usd.has_value()) {
        args.emplace_back("COST_PER_HIT");
        args.push_back(detail::format_nonnegative(*opts.cost_per_hit_usd));
    }
    client_.command(std::move(args));
}

void PCacheCommands::set(std::string_view ns, std::string_view prompt, std::string_view response,
                          std::optional<int64_t> ttl_seconds, std::string_view meta_json,
                          const std::optional<Usage>& usage) {
    std::vector<std::string> args{"PCACHE.SET", std::string(ns), std::string(prompt), std::string(response)};
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

std::optional<std::string> PCacheCommands::get(std::string_view ns, std::string_view prompt) {
    return client_.command({"PCACHE.GET", std::string(ns), std::string(prompt)}).as_optional_string();
}

std::optional<PCacheEntry> PCacheCommands::getex(std::string_view ns, std::string_view prompt) {
    Reply r = client_.command({"PCACHE.GETEX", std::string(ns), std::string(prompt)});
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    PCacheEntry e;
    e.response = arr.at(0).as_string();
    e.metadata = arr.at(1).is_nil() ? std::string() : arr.at(1).as_string();
    e.hit_count = arr.at(2).as_int();
    return e;
}

bool PCacheCommands::del(std::string_view ns, std::string_view prompt) {
    return client_.command({"PCACHE.DEL", std::string(ns), std::string(prompt)}).as_int() != 0;
}

PCacheStats PCacheCommands::stats(std::string_view ns) {
    auto pairs = client_.command({"PCACHE.STATS", std::string(ns)}).as_pairs();
    PCacheStats s;
    s.entries = detail::pairs_int(pairs, "entries");
    s.hits = detail::pairs_int(pairs, "hits");
    s.misses = detail::pairs_int(pairs, "misses");
    s.evictions = detail::pairs_int(pairs, "evictions");
    s.memory_bytes = detail::pairs_int(pairs, "memory_bytes");
    s.hit_rate = detail::pairs_double(pairs, "hit_rate");
    s.p50_ns = detail::pairs_int(pairs, "p50_ns");
    s.p95_ns = detail::pairs_int(pairs, "p95_ns");
    s.p99_ns = detail::pairs_int(pairs, "p99_ns");
    s.cost_saved_usd = detail::pairs_double(pairs, "cost_saved_usd");
    s.usage.requests = detail::pairs_int(pairs, "usage_saved_requests");
    s.usage.input_tokens = detail::pairs_int(pairs, "usage_saved_input_tokens");
    s.usage.output_tokens = detail::pairs_int(pairs, "usage_saved_output_tokens");
    s.usage.cache_read_tokens = detail::pairs_int(pairs, "usage_saved_cache_read_tokens");
    s.usage.cache_write_tokens = detail::pairs_int(pairs, "usage_saved_cache_write_tokens");
    s.usage.cost_usd = detail::pairs_double(pairs, "usage_saved_cost_usd");
    return s;
}

int64_t PCacheCommands::flush(std::string_view ns) {
    return client_.command({"PCACHE.FLUSH", std::string(ns)}).as_int();
}

bool PCacheCommands::drop(std::string_view ns) {
    return client_.command({"PCACHE.DROP", std::string(ns)}).as_int() != 0;
}

std::vector<std::string> PCacheCommands::list() {
    return client_.command({"PCACHE.LIST"}).as_string_array();
}

void PCacheCommands::config_set_max_entries(std::string_view ns, int64_t value) {
    client_.command({"PCACHE.CONFIG", std::string(ns), "SET", "MAX_ENTRIES", std::to_string(value)});
}

void PCacheCommands::config_set_ttl(std::string_view ns, int64_t seconds) {
    client_.command({"PCACHE.CONFIG", std::string(ns), "SET", "TTL", std::to_string(seconds)});
}

void PCacheCommands::config_set_cost_per_hit(std::string_view ns, double usd) {
    client_.command({"PCACHE.CONFIG", std::string(ns), "SET", "COST_PER_HIT", detail::format_nonnegative(usd)});
}

} // namespace hotkv
