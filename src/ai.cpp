// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/ai.hpp>

#include <hotkv/error.hpp>

#include "sha256.hpp"

#include <cstdio>

namespace hotkv::ai {

namespace {

void append_json_string(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // Non-ASCII bytes (UTF-8 continuation/lead bytes are
                    // all >= 0x80) fall straight through as literal
                    // bytes, matching json.dumps(ensure_ascii=False).
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

bool already_exists(const ServerError& e) {
    return std::string(e.what()).find("already exists") != std::string::npos;
}

} // namespace

std::string build_prompt_cache_key(const PromptCacheKeyInput& input) {
    std::string json;
    json += '{';

    json += "\"provider\":";
    append_json_string(json, input.provider);
    json += ',';

    json += "\"model\":";
    append_json_string(json, input.model);
    json += ',';

    json += "\"system\":";
    append_json_string(json, input.system);
    json += ',';

    json += "\"messages\":[";
    for (std::size_t i = 0; i < input.messages.size(); ++i) {
        if (i != 0) {
            json += ',';
        }
        json += '{';
        json += "\"role\":";
        append_json_string(json, input.messages[i].role);
        json += ',';
        json += "\"content\":";
        append_json_string(json, input.messages[i].content);
        json += '}';
    }
    json += "],";

    json += "\"params\":{";
    bool first = true;
    for (const auto& [key, value] : input.params) { // std::map: ascending byte-value key order
        if (!first) {
            json += ',';
        }
        first = false;
        append_json_string(json, key);
        json += ':';
        append_json_string(json, value);
    }
    json += '}';

    json += '}';

    return detail::sha256_hex(json);
}

std::optional<Usage> LlmUsage::as_usage() const {
    if (model.empty()) {
        return std::nullopt;
    }
    Usage u;
    u.model = model;
    u.input_tokens = static_cast<int64_t>(input_tokens);
    u.output_tokens = static_cast<int64_t>(output_tokens);
    u.cache_read_tokens = static_cast<int64_t>(cache_read_tokens);
    u.cache_write_5m_tokens = static_cast<int64_t>(cache_write_5m_tokens);
    u.cache_write_1h_tokens = static_cast<int64_t>(cache_write_1h_tokens);
    return u;
}

PromptCache::PromptCache(Client client, std::string ns, std::optional<int64_t> ttl_seconds)
    : client_(std::move(client)), ns_(std::move(ns)), ttl_seconds_(ttl_seconds) {}

PromptCache& PromptCache::with_stats_bucket(std::string bucket) {
    stats_bucket_ = std::move(bucket);
    return *this;
}

void PromptCache::create_namespace(std::optional<int64_t> max_entries, std::optional<double> cost_per_hit_usd) {
    PCacheCreateOptions opts;
    opts.max_entries = max_entries;
    opts.ttl_seconds = ttl_seconds_;
    opts.cost_per_hit_usd = cost_per_hit_usd;
    try {
        client_.pcache().create(ns_, opts);
    } catch (const ServerError& e) {
        if (!already_exists(e)) {
            throw;
        }
    }
}

PromptCache::Result PromptCache::get_or_call(const PromptCacheKeyInput& input,
                                              const std::function<LlmCallResult()>& fn) {
    std::string key = build_prompt_cache_key(input);
    auto start = std::chrono::steady_clock::now();

    try {
        std::optional<std::string> cached = client_.pcache().get(ns_, key);
        if (cached.has_value()) {
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            LlmUsage usage;
            usage.latency = latency;
            record(usage, /*hit=*/true);
            return Result{*cached, true};
        }
    } catch (const NotLicensedError&) {
        // The cache itself is unavailable, not the LLM: fall through to fn().
    }

    LlmCallResult result = fn();

    try {
        client_.pcache().set(ns_, key, result.response, ttl_seconds_, {}, result.usage.as_usage());
    } catch (const NotLicensedError&) {
        // Nothing to do: the miss path already has the answer to return.
    }
    record(result.usage, /*hit=*/false);

    return Result{result.response, false};
}

PCacheStats PromptCache::stats() {
    return client_.pcache().stats(ns_);
}

void PromptCache::record(const LlmUsage& usage, bool hit) {
    if (stats_bucket_.empty()) {
        return;
    }
    int64_t input_tokens = hit ? 0 : static_cast<int64_t>(usage.input_tokens);
    int64_t output_tokens = hit ? 0 : static_cast<int64_t>(usage.output_tokens);
    int64_t cached_tokens = hit ? static_cast<int64_t>(usage.input_tokens + usage.output_tokens) : 0;
    double cost = hit ? 0.0 : usage.cost_usd;
    LlmStatsRecordOptions opts;
    if (!hit && !usage.model.empty()) {
        opts.model = usage.model;
        opts.cache_write_5m_tokens = static_cast<int64_t>(usage.cache_write_5m_tokens);
        opts.cache_write_1h_tokens = static_cast<int64_t>(usage.cache_write_1h_tokens);
    }
    try {
        client_.llmstats().record(stats_bucket_, input_tokens, output_tokens, cached_tokens, cost,
                                   usage.latency.count(), opts);
    } catch (const ServerError&) {
        // Best-effort accounting: never let a stats hiccup fail the caller's request.
    } catch (const InvalidArgumentError&) {
        // A callback-supplied cost the server could never accept (negative,
        // NaN): same rule, the LLM call already happened and succeeded.
    }
}

SemanticCache::SemanticCache(Client client, std::string ns, std::optional<int64_t> ttl_seconds)
    : client_(std::move(client)), ns_(std::move(ns)), ttl_seconds_(ttl_seconds) {}

void SemanticCache::create_namespace(int64_t dims, std::string_view metric, std::optional<double> threshold) {
    SCacheCreateOptions opts;
    opts.metric = std::string(metric);
    opts.threshold = threshold;
    opts.ttl_seconds = ttl_seconds_;
    try {
        client_.scache().create(ns_, dims, opts);
    } catch (const ServerError& e) {
        if (!already_exists(e)) {
            throw;
        }
    }
}

SemanticCache::Result SemanticCache::get_or_call(std::string_view query, const std::vector<double>& vector,
                                                  const std::function<LlmCallResult()>& fn) {
    try {
        std::optional<SCacheHit> hit = client_.scache().get(ns_, vector);
        if (hit.has_value()) {
            return Result{hit->response, true};
        }
    } catch (const NotLicensedError&) {
        // Fall through to fn(): the cache is unavailable, not the LLM.
    }

    LlmCallResult result = fn();

    try {
        client_.scache().set(ns_, query, result.response, vector, ttl_seconds_, result.usage.as_usage());
    } catch (const NotLicensedError&) {
        // Nothing to do: the miss path already has the answer to return.
    }

    return Result{result.response, false};
}

SCacheStats SemanticCache::stats() {
    return client_.scache().stats(ns_);
}

} // namespace hotkv::ai
