// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/client.hpp>
#include <hotkv/enterprise.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Idiomatic, provider-neutral helpers layered on top of the raw PCACHE.*
// and SCACHE.* commands. Neither class depends
// on any particular LLM or embedding provider's SDK; callers supply
// messages/vectors and a callback that performs the actual provider
// call.

namespace hotkv::ai {

struct PromptMessage {
    std::string role;
    std::string content;
};

// The normalized shape a PromptCache key is derived from. `params`
// values must already be caller-formatted strings (e.g. "0.7", not
// 0.7), so the same logical request hashes identically regardless of
// language or float formatting; std::map keeps them in ascending
// byte-value key order automatically, matching the canonical form.
struct PromptCacheKeyInput {
    std::string provider;
    std::string model;
    std::string system;
    std::vector<PromptMessage> messages;
    std::map<std::string, std::string> params;
};

// Deterministically derives a cache key from `input`: a canonical,
// compact, sorted-key JSON encoding of the request, SHA-256 hashed and
// returned as a lowercase hex string (64 characters). Every official
// HotKV SDK implements the exact same canonical form, so keys are
// shareable across languages.
std::string build_prompt_cache_key(const PromptCacheKeyInput& input);

// Token/cost/latency accounting for one LLM call, returned by the
// caller's model function on a PromptCache/SemanticCache miss. model,
// when non-empty, does two things: it enables LLMSTATS.RECORD based
// accounting (PromptCache::with_stats_bucket) to price the call from the
// server's price table, and it is attached to the stored cache entry
// (the PCACHE.SET/SCACHE.SET usage group, see Usage) so a later hit
// attributes the tokens and cost that hit did not have to spend.
// cache_read_tokens is provider prompt-cache hit tokens;
// cache_write_5m_tokens/cache_write_1h_tokens are Anthropic-style
// cache-write tokens by TTL tier (leave both 0 for providers without a
// separate cache-write cost, e.g. OpenAI).
struct LlmUsage {
    std::string model;
    uint64_t input_tokens = 0;
    uint64_t output_tokens = 0;
    uint64_t cache_read_tokens = 0;
    uint64_t cache_write_5m_tokens = 0;
    uint64_t cache_write_1h_tokens = 0;
    double cost_usd = 0.0;
    std::chrono::milliseconds latency{0};

    // This usage as a cache-entry Usage group, or nullopt when model is
    // empty (Usage requires one).
    std::optional<Usage> as_usage() const;
};

// What the caller-supplied function passed to PromptCache::get_or_call
// must return on a cache miss.
struct LlmCallResult {
    std::string response;
    LlmUsage usage;
};

// An idiomatic get-or-call cache in front of PCACHE.*: on a hit it
// returns the stored response without invoking the LLM; on a miss it
// calls the caller's function, stores the result, and returns it.
// PromptCache needs a server license with the prompt-cache feature; a
// NotLicensedError from the cache itself is swallowed (falls through to
// calling fn, since the cache being unavailable is not the caller's LLM
// failing) rather than propagated.
class PromptCache {
public:
    // Stores entries in PCACHE namespace `ns` with the given default TTL
    // (nullopt = no expiry). Call create_namespace() once before first
    // use.
    PromptCache(Client client, std::string ns, std::optional<int64_t> ttl_seconds = std::nullopt);

    // Enables LLMSTATS.RECORD accounting on every get_or_call (hit or
    // miss) against the named bucket. Returns *this for chaining.
    PromptCache& with_stats_bucket(std::string bucket);

    // Ensures the PCACHE namespace backing this PromptCache exists;
    // tolerates the namespace already existing.
    void create_namespace(std::optional<int64_t> max_entries = std::nullopt,
                           std::optional<double> cost_per_hit_usd = std::nullopt);

    struct Result {
        std::string response;
        bool hit = false;
    };

    // Returns the cached response for `input` if present; otherwise
    // calls fn(), caches its response together with any usage it reports
    // (model required for it to be attached; see LlmUsage), and returns
    // that.
    Result get_or_call(const PromptCacheKeyInput& input, const std::function<LlmCallResult()>& fn);

    // The current PCACHE.STATS for this cache's namespace, including how
    // many tokens and how much cost recorded usage has saved on hits.
    PCacheStats stats();

private:
    void record(const LlmUsage& usage, bool hit);

    Client client_;
    std::string ns_;
    std::optional<int64_t> ttl_seconds_;
    std::string stats_bucket_;
};

// An idiomatic get-or-call cache in front of SCACHE.*: looks up the
// nearest cached answer to a caller-supplied embedding vector above the
// namespace's similarity threshold, calling the caller's function only
// on a miss. Never computes embeddings itself. Needs a server
// license with the semantic-cache feature (NotLicensedError from the
// cache is likewise swallowed, falling through to fn()).
class SemanticCache {
public:
    // Stores entries in SCACHE namespace `ns` with the given default TTL
    // (nullopt = no expiry). Call create_namespace() once before first
    // use.
    SemanticCache(Client client, std::string ns, std::optional<int64_t> ttl_seconds = std::nullopt);

    // Ensures the SCACHE namespace exists, with the given embedding
    // dimensionality, distance metric ("" defaults to cosine) and
    // similarity threshold (nullopt uses the engine default, 0.85).
    // Tolerates the namespace already existing.
    void create_namespace(int64_t dims, std::string_view metric = {},
                           std::optional<double> threshold = std::nullopt);

    struct Result {
        std::string response;
        bool hit = false;
    };

    // Returns the cached answer for the nearest neighbor of `vector`
    // above the namespace's similarity threshold, if any; otherwise
    // calls fn(), caches (query, response, vector) together with any
    // usage it reports, and returns the fresh answer.
    Result get_or_call(std::string_view query, const std::vector<double>& vector,
                        const std::function<LlmCallResult()>& fn);

    // The current SCACHE.STATS for this cache's namespace, including how
    // many tokens and how much cost recorded usage has saved on hits.
    SCacheStats stats();

private:
    Client client_;
    std::string ns_;
    std::optional<int64_t> ttl_seconds_;
};

} // namespace hotkv::ai
