// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Small result structs for the HotKV exclusive and AI commands whose
// reply is a fixed-shape record rather than a single scalar or a
// homogeneous list. See the doc comment on the corresponding accessor method in
// <hotkv/enterprise.hpp> for which command produces each one.

namespace hotkv {

// Shared "token usage group": pass it to a cache
// set() (SCacheCommands::set, PCacheCommands::set, KeyedCacheCommands::set)
// or to LlmBudgetCommands::consume so a later cache hit (or the budget's
// own accounting) attributes the tokens, and once model is priced
// (LlmStatsCommands::price_set), the cost, that original call spent.
//
// model is required whenever a Usage is sent at all: leave the enclosing
// std::optional<Usage> argument as std::nullopt to omit the group
// entirely, not a Usage with an empty model. The token counts default to
// 0 and are left off the wire when zero; MODEL itself is always sent
// whenever a Usage is passed.
struct Usage {
    std::string model;
    int64_t input_tokens = 0;       // Uncached prompt tokens.
    int64_t output_tokens = 0;      // Output/completion tokens.
    int64_t cache_read_tokens = 0;  // Provider prompt-cache hit tokens.
    int64_t cache_write_5m_tokens = 0; // Anthropic-style 5-minute-TTL cache-write tokens.
    int64_t cache_write_1h_tokens = 0; // Anthropic-style 1-hour-TTL cache-write tokens.
};

// The tokens (and, once the model is priced, the cost) a cache's hits did
// not have to spend, accumulated from Usage values recorded on set().
// Every field is zero when no set() call ever carried a Usage. Shared by
// SCacheStats, PCacheStats and KeyedCacheStats.
struct UsageSavings {
    int64_t requests = 0;
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    int64_t cache_read_tokens = 0;
    int64_t cache_write_tokens = 0;
    double cost_usd = 0.0;
};

struct KeyedCacheStats {
    int64_t entries = 0;
    int64_t hits = 0;
    int64_t misses = 0;
    int64_t evictions = 0;
    int64_t memory_bytes = 0;
    UsageSavings usage;
};

struct SCacheStats {
    int64_t entries = 0;
    int64_t hits = 0;
    int64_t misses = 0;
    double hit_rate = 0.0;
    int64_t p50_us = 0;
    UsageSavings usage;
};

struct SCacheHit {
    std::string response;
    double similarity = 0.0;
};

struct PCacheStats {
    int64_t entries = 0;
    int64_t hits = 0;
    int64_t misses = 0;
    int64_t evictions = 0;
    int64_t memory_bytes = 0;
    double hit_rate = 0.0;
    int64_t p50_ns = 0;
    int64_t p95_ns = 0;
    int64_t p99_ns = 0;
    double cost_saved_usd = 0.0;
    UsageSavings usage;
};

struct PCacheEntry {
    std::string response;
    std::string metadata;
    int64_t hit_count = 0;
};

struct GcraResult {
    bool denied = false;
    int64_t limit = 0;
    int64_t remaining = 0;
    int64_t retry_after_ms = -1;
    int64_t reset_after_ms = 0;
};

struct VsimIndexInfo {
    std::string name;
    int64_t dimensions = 0;
    std::string metric;
    int64_t vectors = 0;
    int64_t m = 0;
    int64_t ef_construction = 0;
    int64_t ef_search = 0;
};

struct VsimSearchResult {
    std::string key;
    double distance = 0.0;
};

struct LlmStatsSnapshot {
    int64_t requests = 0;
    int64_t errors = 0;
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    int64_t cached_tokens = 0;
    double cost_usd = 0.0;
    double avg_latency_ms = 0.0;
    int64_t requests_1m = 0;
    int64_t tokens_1m = 0;
    double cost_usd_1m = 0.0;
    int64_t cache_write_5m_tokens = 0;
    int64_t cache_write_1h_tokens = 0;
    int64_t unpriced_requests = 0;
};

// One model's decoded price sheet, from LlmStatsCommands::price_get()/
// price_list(). source is "default", "custom" or "unpriced" (an unknown
// model is not an error: it comes back with source "unpriced", priced
// false). Prices are USD per 1,000,000 tokens.
struct LlmPrice {
    std::string model;
    std::string source;
    bool priced = false;
    bool retired = false;
    double input_usd_per_mtok = 0.0;
    double output_usd_per_mtok = 0.0;
    double cache_read_usd_per_mtok = 0.0;
    double cache_write_5m_usd_per_mtok = 0.0;
    double cache_write_1h_usd_per_mtok = 0.0;
};

// Optional trailing keywords of LLMSTATS.RECORD.
// When model is non-empty, the server computes cost from its price table
// (see LlmStatsCommands::price_set) and ignores record()'s cost_usd
// argument (pass 0); cache_write_5m_tokens/cache_write_1h_tokens (Anthropic
// cache-write tokens by TTL tier) are only meaningful together with model.
struct LlmStatsRecordOptions {
    std::optional<bool> error;
    std::string model;
    int64_t cache_write_5m_tokens = 0;
    int64_t cache_write_1h_tokens = 0;
};

// Optional cache price overrides for LlmStatsCommands::price_set(). A
// field left nullopt defaults (server side) to 0.1x (cache read), 1.25x
// (5-minute cache write) or 2x (1-hour cache write) the input price.
struct LlmPriceOptions {
    std::optional<double> cache_read_usd_per_mtok;
    std::optional<double> cache_write_5m_usd_per_mtok;
    std::optional<double> cache_write_1h_usd_per_mtok;
};

struct LlmBudgetStatus {
    int64_t tokens_used = 0;
    int64_t token_limit = 0;
    double cost_used_usd = 0.0;
    double cost_limit_usd = 0.0;
    int64_t window_seconds = 0;
    int64_t window_remaining = 0;
    int64_t requests_allowed = 0;
    int64_t requests_rejected = 0;
    int64_t unpriced_requests = 0;
};

struct PromptVersion {
    int64_t version = 0;
    std::string content;
    std::string metadata;
    int64_t created_unix = 0;
};

struct EmbedDedupStats {
    int64_t hits = 0;
    int64_t misses = 0;
    int64_t entries = 0;
};

struct AmemTurn {
    std::string role;
    std::string content;
};

struct AmemRecallResult {
    std::string key;
    std::string content;
    double similarity = 0.0;
};

struct AmemSearchResult {
    std::string source;
    std::string key;
    std::string content;
    double similarity = 0.0;
};

struct AmemEpisodicEvent {
    std::string type;
    std::string content;
    std::string metadata;
    int64_t timestamp_ns = 0;
};

struct AmemEpisodicStats {
    int64_t total_events = 0;
    int64_t oldest_ts = 0;
    int64_t newest_ts = 0;
};

struct AmemSummary {
    std::string summary;
    int64_t version = 0;
    int64_t updated_unix = 0;
};

struct RagSearchResult {
    std::string chunk_id;
    std::string doc_id;
    std::string text;
    double score = 0.0;
    double vector_score = 0.0;
    double bm25_score = 0.0;
    std::vector<std::pair<std::string, std::string>> metadata;
};

struct RagStats {
    std::string name;
    int64_t dimensions = 0;
    int64_t total_docs = 0;
    int64_t total_chunks = 0;
    int64_t total_ingested = 0;
    int64_t total_queries = 0;
    int64_t chunk_size = 0;
    std::string chunk_strategy;
    int64_t chunk_overlap = 0;
};

struct RateLimitCheckResult {
    bool allowed = false;
    int64_t remaining = 0;
    int64_t retry_after_ms = 0;
    int64_t limit = 0;
    int64_t current = 0;
};

struct RateLimitStats {
    std::string name;
    std::string algo;
    int64_t limit = 0;
    int64_t window_ms = 0;
    int64_t unique_keys = 0;
    int64_t total_allowed = 0;
    int64_t total_denied = 0;
};

struct TsSample {
    int64_t timestamp_ms = 0;
    double value = 0.0;
};

struct TsInfo {
    std::string key;
    int64_t total_samples = 0;
    int64_t first_timestamp = 0;
    int64_t last_timestamp = 0;
    double last_value = 0.0;
    int64_t retention_ms = 0;
    int64_t memory_bytes = 0;
    std::vector<std::pair<std::string, std::string>> labels;
    int64_t rules = 0;
};

struct FeatureRecord {
    std::vector<std::pair<std::string, std::string>> values;
    int64_t updated_at_ms = 0;
    int64_t freshness_ms = 0;
};

struct FeatureSchemaField {
    std::string name;
    std::string type;
};

struct FeatureStats {
    int64_t entities = 0;
    int64_t total_reads = 0;
    int64_t total_writes = 0;
    int64_t memory_bytes = 0;
};

struct ObserveInfo {
    int64_t uptime_seconds = 0;
    int64_t connected_clients = 0;
    int64_t total_commands = 0;
    int64_t used_memory_bytes = 0;
    int64_t peak_memory_bytes = 0;
    int64_t total_connections = 0;
    std::string enterprise_features;
};

struct ObserveMetrics {
    int64_t uptime_seconds = 0;
    int64_t connected_clients = 0;
    int64_t total_commands = 0;
    int64_t total_connections = 0;
    int64_t used_memory_bytes = 0;
    int64_t peak_memory_bytes = 0;
    int64_t slowlog_length = 0;
    int64_t alerts_sent = 0;
    int64_t alerts_failed = 0;
    int64_t num_workers = 0;
};

struct AlertsInfo {
    bool enabled = false;
    bool flag_enabled = false;
    std::string webhook_url;
    std::string provider;
    std::string supported_providers;
    std::string min_severity;
    int64_t alerts_sent = 0;
    int64_t alerts_failed = 0;
    int64_t alerts_dropped = 0;
    int64_t alerts_rate_limited = 0;
    int64_t alerts_deduped = 0;
    int64_t alerts_filtered = 0;
    int64_t alerts_queued = 0;
};

} // namespace hotkv
