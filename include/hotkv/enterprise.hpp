// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/client.hpp>
#include <hotkv/enterprise_types.hpp>
#include <hotkv/reply.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Typed wrappers for the HotKV exclusive and AI commands, grouped into
// accessor objects by command family. Every enterprise (non-core) family below
// throws hotkv::NotLicensedError, instead of returning, when the
// server's license does not cover it; TAG.*, NEXPIRE/NTTL/NEXPIREAT (on
// Client directly), HOTKEYS, GCRA.* and the non-persistence VSIM.*
// commands need no license.
//
// Access each group through hotkv::Client (client.tag(), client.pcache(),
// ...); the constructors here are public only so the accessor types are
// simple, freely constructible values (each just wraps a Client).

namespace hotkv {

// TAG.* (core)
class TagCommands {
public:
    explicit TagCommands(Client client) : client_(std::move(client)) {}

    int64_t set(std::string_view key, const std::vector<std::string>& tags);
    int64_t rem(std::string_view key, const std::vector<std::string>& tags);
    std::vector<std::string> get(std::string_view key);
    std::vector<std::string> members(std::string_view tag);
    std::vector<std::string> inter(const std::vector<std::string>& tags);
    int64_t count(std::string_view key);
    bool exists(std::string_view key, std::string_view tag);

private:
    Client client_;
};

// HOTKEYS (core, admin)
class HotKeysCommands {
public:
    explicit HotKeysCommands(Client client) : client_(std::move(client)) {}

    std::vector<std::pair<std::string, int64_t>> top(int64_t count = 10);
    void reset();

private:
    Client client_;
};

// GCRA.* (core rate limiter primitive)
class GcraCommands {
public:
    explicit GcraCommands(Client client) : client_(std::move(client)) {}

    GcraResult limit(std::string_view key, int64_t period_ms, int64_t burst, int64_t count = 1);
    int64_t reset(const std::vector<std::string>& keys);

private:
    Client client_;
};

// Optional tuning for VsimCommands::create_index(); empty/nullopt fields
// use the server's own defaults.
struct VsimCreateOptions {
    std::string metric; // "" (server default cosine), "cosine", "l2" or "ip"
    std::optional<int64_t> m;
    std::optional<int64_t> ef_construction;
    std::optional<int64_t> ef_search;
};

// VSIM.* (core, except persistence) and VSIM.SAVE/LOAD/LIST/DROP
// (persistence; needs a license with the VSIM persistence feature).
class VsimCommands {
public:
    explicit VsimCommands(Client client) : client_(std::move(client)) {}

    void create_index(std::string_view name, int64_t dims, const VsimCreateOptions& opts = {});
    void drop_index(std::string_view name);
    // Returns true if `key` was newly inserted, false if it replaced an
    // existing vector.
    bool set(std::string_view index, std::string_view key, const std::vector<double>& vector);
    std::optional<std::vector<double>> get(std::string_view index, std::string_view key);
    bool del(std::string_view index, std::string_view key);
    std::vector<VsimSearchResult> search(std::string_view index, int64_t k, const std::vector<double>& vector,
                                          const std::vector<std::string>& filter_tags = {},
                                          std::optional<int64_t> ef = std::nullopt);
    // VSIM.INFO with no index: all index names.
    std::vector<std::string> list_indexes();
    // VSIM.INFO <index>.
    VsimIndexInfo info(std::string_view index);

    // Persistence (VSIM.SAVE/LOAD/LIST/DROP): snapshot names are
    // distinct from, and unrelated to, index names above.
    int64_t save(std::string_view snapshot_name, const std::string& blob);
    std::optional<std::string> load(std::string_view snapshot_name);
    std::vector<std::string> list_snapshots();
    bool drop_snapshot(std::string_view snapshot_name);

private:
    Client client_;
};

// SCACHE.* (semantic cache; license feature: semantic-cache)
struct SCacheCreateOptions {
    std::string metric; // "" (default cosine), "cosine", "l2" or "ip"
    std::optional<double> threshold;
    std::optional<int64_t> ttl_seconds;
};

class SCacheCommands {
public:
    explicit SCacheCommands(Client client) : client_(std::move(client)) {}

    void create(std::string_view ns, int64_t dims, const SCacheCreateOptions& opts = {});
    // usage is optional (nullopt omits it): when given, it records the
    // original (uncached) call's token usage so a later get() hit
    // attributes the tokens and cost that hit did not have to spend (see
    // stats()).
    void set(std::string_view ns, std::string_view query_text, std::string_view response,
              const std::vector<double>& vector, std::optional<int64_t> ttl_seconds = std::nullopt,
              const std::optional<Usage>& usage = std::nullopt);
    // nullopt on a miss (no entry above threshold).
    std::optional<SCacheHit> get(std::string_view ns, const std::vector<double>& vector,
                                  std::optional<double> threshold_override = std::nullopt);
    SCacheStats stats(std::string_view ns);
    bool flush(std::string_view ns);
    bool drop(std::string_view ns);
    std::vector<std::string> list();
    void set_threshold(std::string_view ns, double value);

private:
    Client client_;
};

// PCACHE.* (prompt/LLM response cache; license feature: prompt-cache)
struct PCacheCreateOptions {
    std::optional<int64_t> max_entries;
    std::optional<int64_t> ttl_seconds;
    std::optional<double> cost_per_hit_usd;
};

class PCacheCommands {
public:
    explicit PCacheCommands(Client client) : client_(std::move(client)) {}

    void create(std::string_view ns, const PCacheCreateOptions& opts = {});
    // usage is optional (nullopt omits it): when given, it records the
    // original (uncached) call's token usage so a later get()/getex()
    // hit attributes the tokens and cost that hit did not have to spend
    // (see stats()).
    void set(std::string_view ns, std::string_view prompt, std::string_view response,
              std::optional<int64_t> ttl_seconds = std::nullopt, std::string_view meta_json = {},
              const std::optional<Usage>& usage = std::nullopt);
    std::optional<std::string> get(std::string_view ns, std::string_view prompt);
    std::optional<PCacheEntry> getex(std::string_view ns, std::string_view prompt);
    bool del(std::string_view ns, std::string_view prompt);
    PCacheStats stats(std::string_view ns);
    int64_t flush(std::string_view ns);
    bool drop(std::string_view ns);
    std::vector<std::string> list();
    void config_set_max_entries(std::string_view ns, int64_t value);
    void config_set_ttl(std::string_view ns, int64_t seconds);
    void config_set_cost_per_hit(std::string_view ns, double usd);

private:
    Client client_;
};

// Keyed caches: shared implementation behind TOOL.CACHE.*, NCACHE.*,
// GUARD.* and RERANK.* (each has its own license feature), reached via
// Client::tool_cache()/ncache()/guard()/rerank().
class KeyedCacheCommands {
public:
    KeyedCacheCommands(Client client, std::string wire_prefix)
        : client_(std::move(client)), prefix_(std::move(wire_prefix)) {}

    void create(std::string_view ns, std::optional<int64_t> max_entries = std::nullopt,
                std::optional<int64_t> ttl_seconds = std::nullopt);
    // usage is optional (nullopt omits it): when given, it records the
    // original (uncached) call's token usage so a later get() hit
    // attributes the tokens and cost that hit did not have to spend (see
    // stats()).
    void set(std::string_view ns, std::string_view key, std::string_view value,
             std::optional<int64_t> ttl_seconds = std::nullopt, std::string_view meta_json = {},
             const std::optional<Usage>& usage = std::nullopt);
    std::optional<std::string> get(std::string_view ns, std::string_view key);
    bool del(std::string_view ns, std::string_view key);
    KeyedCacheStats stats(std::string_view ns);
    int64_t flush(std::string_view ns);
    bool drop(std::string_view ns);
    std::vector<std::string> list();

private:
    Client client_;
    std::string prefix_;
};

// LLMSTATS.* (license feature: LLM stats)
class LlmStatsCommands {
public:
    explicit LlmStatsCommands(Client client) : client_(std::move(client)) {}

    void bucket(std::string_view name);
    // cached_tokens is cache-READ tokens. opts.model, when non-empty,
    // makes the server compute cost from its price table (see price_set())
    // instead of cost_usd (pass 0); for OpenAI, pass
    // prompt_tokens - cached_tokens as input_tokens.
    void record(std::string_view bucket, int64_t input_tokens, int64_t output_tokens, int64_t cached_tokens,
                double cost_usd, int64_t latency_ms, const LlmStatsRecordOptions& opts = {});
    LlmStatsSnapshot get(std::string_view bucket);
    bool reset(std::string_view bucket);
    bool drop(std::string_view bucket);
    std::vector<std::string> list();

    // Sets (or replaces) a custom price override for model, shared with
    // record()'s and LlmBudgetCommands::consume()'s model option. Prices
    // are USD per 1,000,000 tokens.
    void price_set(std::string_view model, double input_usd_per_mtok, double output_usd_per_mtok,
                    const LlmPriceOptions& opts = {});
    // An unknown model replies with source "unpriced", priced false, not an error.
    LlmPrice price_get(std::string_view model);
    // Every known model's price sheet (built-in and custom).
    std::vector<LlmPrice> price_list();
    // Returns true iff a custom override existed and was removed (the
    // model then falls back to its built-in price, if any).
    bool price_del(std::string_view model);

private:
    Client client_;
};

// LLMBUDGET.* (license feature: LLM budget)
class LlmBudgetCommands {
public:
    explicit LlmBudgetCommands(Client client) : client_(std::move(client)) {}

    void set(std::string_view name, int64_t token_limit, double cost_limit_usd, int64_t window_seconds);
    // false if this consumption would exceed the token or cost cap. usage
    // is optional (nullopt omits it): when given, tokens/cost_usd are
    // ignored server side (pass 0/0.0): the token count is the sum of
    // usage's counts, and the cost is computed from the price table (see
    // LlmStatsCommands::price_set); an unpriced model still consumes the
    // token budget but costs $0, counted in LlmBudgetStatus::unpriced_requests.
    bool consume(std::string_view name, int64_t tokens, double cost_usd,
                 const std::optional<Usage>& usage = std::nullopt);
    LlmBudgetStatus status(std::string_view name);
    bool reset(std::string_view name);
    bool drop(std::string_view name);
    std::vector<std::string> list();

private:
    Client client_;
};

// PROMPT.* (versioned prompt/template registry; license feature: prompt-registry)
class PromptRegistryCommands {
public:
    explicit PromptRegistryCommands(Client client) : client_(std::move(client)) {}

    int64_t put(std::string_view name, std::string_view content, std::string_view meta_json = {});
    // Omitting version (or 0) returns the latest version.
    std::optional<PromptVersion> get(std::string_view name, int64_t version = 0);
    std::optional<PromptVersion> latest(std::string_view name);
    void set_active(std::string_view name, int64_t version);
    std::vector<int64_t> versions(std::string_view name);
    bool drop(std::string_view name);
    std::vector<std::string> list();

private:
    Client client_;
};

// EMBED.DEDUP.* (license feature: embed-dedup)
class EmbedDedupCommands {
public:
    explicit EmbedDedupCommands(Client client) : client_(std::move(client)) {}

    bool put(std::string_view ns, std::string_view hash, std::string_view model, std::string_view embedding);
    std::optional<std::string> get(std::string_view ns, std::string_view hash);
    bool release(std::string_view ns, std::string_view hash);
    EmbedDedupStats stats(std::string_view ns);
    bool drop(std::string_view ns);
    bool purge(std::string_view ns);
    std::vector<std::string> list();

private:
    Client client_;
};

// AMEM.CONV.* (conversation memory)
class AmemConvCommands {
public:
    explicit AmemConvCommands(Client client) : client_(std::move(client)) {}

    void create(std::string_view session_id, std::optional<int64_t> window = std::nullopt,
                std::optional<int64_t> ttl_seconds = std::nullopt);
    void append(std::string_view session_id, std::string_view role, std::string_view content);
    std::vector<AmemTurn> get(std::string_view session_id);
    std::vector<AmemTurn> window(std::string_view session_id, int64_t n);
    bool clear(std::string_view session_id);
    bool drop(std::string_view session_id);

private:
    Client client_;
};

// AMEM.SEM.* (per-agent semantic vector memory)
class AmemSemCommands {
public:
    explicit AmemSemCommands(Client client) : client_(std::move(client)) {}

    void create(std::string_view agent_id, int64_t dimensions);
    void store(std::string_view agent_id, std::string_view key, std::string_view content,
               const std::vector<double>& vector, std::optional<int64_t> ttl_seconds = std::nullopt);
    std::vector<AmemRecallResult> recall(std::string_view agent_id, int64_t k, const std::vector<double>& vector);
    bool forget(std::string_view agent_id, std::string_view key);
    std::vector<std::string> list(std::string_view agent_id);

private:
    Client client_;
};

// AMEM.WORK.* (per-session scratch key/value memory)
class AmemWorkCommands {
public:
    explicit AmemWorkCommands(Client client) : client_(std::move(client)) {}

    void set(std::string_view session_id, std::string_view key, std::string_view value,
             std::optional<int64_t> ttl_seconds = std::nullopt);
    // Note: per the server, an empty stored value is indistinguishable
    // from a miss (both reply nil).
    std::optional<std::string> get(std::string_view session_id, std::string_view key);
    bool del(std::string_view session_id, std::string_view key);
    bool clear(std::string_view session_id);
    std::vector<std::string> list(std::string_view session_id);

private:
    Client client_;
};

// AMEM.EPIS.* (append-only episodic event log)
struct AmemEpisQueryOptions {
    std::string type;
    std::optional<int64_t> from_ts;
    std::optional<int64_t> to_ts;
    std::optional<int64_t> limit;
};

class AmemEpisCommands {
public:
    explicit AmemEpisCommands(Client client) : client_(std::move(client)) {}

    void log(std::string_view agent_id, std::string_view event_type, std::string_view content,
             std::string_view metadata_json = {});
    std::vector<AmemEpisodicEvent> query(std::string_view agent_id, const AmemEpisQueryOptions& opts = {});
    bool clear(std::string_view agent_id);
    AmemEpisodicStats stats(std::string_view agent_id);

private:
    Client client_;
};

// AMEM.SUMMARY.* (license feature: agent-memory summary)
class AmemSummaryCommands {
public:
    explicit AmemSummaryCommands(Client client) : client_(std::move(client)) {}

    int64_t set(std::string_view agent_id, std::string_view summary);
    int64_t append(std::string_view agent_id, std::string_view extra);
    std::optional<AmemSummary> get(std::string_view agent_id);
    bool drop(std::string_view agent_id);
    std::vector<std::string> list();

private:
    Client client_;
};

// AMEM.* umbrella (license feature: agent-memory, except .summary() which
// has its own license feature) and AMEM.SEARCH.
class AgentMemoryCommands {
public:
    explicit AgentMemoryCommands(Client client) : client_(std::move(client)) {}

    AmemConvCommands conv() { return AmemConvCommands(client_); }
    AmemSemCommands sem() { return AmemSemCommands(client_); }
    AmemWorkCommands work() { return AmemWorkCommands(client_); }
    AmemEpisCommands epis() { return AmemEpisCommands(client_); }
    AmemSummaryCommands summary() { return AmemSummaryCommands(client_); }

    // Cross-memory search: semantic + episodic + working memory together.
    std::vector<AmemSearchResult> search(std::string_view agent_id, int64_t k, const std::vector<double>& vector);

private:
    Client client_;
};

// RAG.* (license feature: RAG)
struct RagCreateOptions {
    std::string strategy; // "" (default "fixed"), "fixed" (alias "fixed_size"), "sentence" or "paragraph"
    std::optional<int64_t> chunk_size;
    std::optional<int64_t> chunk_overlap;
};

struct RagIngestOptions {
    std::vector<std::pair<std::string, std::string>> meta;
    // One pre-formatted comma-joined float list per chunk expected from
    // `text` (e.g. {"0.1,0.2,0.3", "0.4,0.5,0.6"}); leave empty to let
    // the server chunk without client-supplied embeddings.
    std::vector<std::string> vectors_csv;
};

struct RagSearchOptions {
    std::optional<int64_t> k;
    std::optional<double> alpha;
    std::optional<std::vector<double>> vector;
    std::vector<std::pair<std::string, std::string>> filter;
};

class RagCommands {
public:
    explicit RagCommands(Client client) : client_(std::move(client)) {}

    void create(std::string_view name, int64_t dimensions, const RagCreateOptions& opts = {});
    int64_t ingest(std::string_view pipeline, std::string_view doc_id, std::string_view text,
                    const RagIngestOptions& opts = {});
    std::vector<RagSearchResult> search(std::string_view pipeline, std::string_view query_text,
                                         const RagSearchOptions& opts = {});
    RagStats stats(std::string_view pipeline);
    void drop(std::string_view pipeline);
    std::vector<std::string> list();

private:
    Client client_;
};

// RATELIMIT.* (license feature: rate-limiting)
struct RateLimitCreateOptions {
    std::string algo; // "" (default sliding_window), "sliding_window", "token_bucket" or "leaky_bucket"
    std::optional<double> refill;
    std::optional<double> drain;
};

class RateLimitCommands {
public:
    explicit RateLimitCommands(Client client) : client_(std::move(client)) {}

    void create(std::string_view name, int64_t limit, int64_t window_ms, const RateLimitCreateOptions& opts = {});
    // Mutating: advances limiter state.
    RateLimitCheckResult check(std::string_view name, std::string_view key, int64_t cost = 1);
    // Non-mutating.
    RateLimitCheckResult peek(std::string_view name, std::string_view key);
    void reset(std::string_view name, std::string_view key);
    RateLimitStats stats(std::string_view name);
    void drop(std::string_view name);
    std::vector<std::string> list();

private:
    Client client_;
};

// TS.* (time series; license feature: time-series)
struct TsCreateOptions {
    std::optional<int64_t> retention_ms;
    std::vector<std::pair<std::string, std::string>> labels;
};

class TimeSeriesCommands {
public:
    explicit TimeSeriesCommands(Client client) : client_(std::move(client)) {}

    void create(std::string_view key, const TsCreateOptions& opts = {});
    // nullopt timestamp sends the literal "*" (server-assigned current time).
    int64_t add(std::string_view key, std::optional<int64_t> timestamp_ms, double value);
    // from_ms/to_ms accept "-" (0) and "+" (max int64) as well as a numeric string.
    std::vector<TsSample> range(std::string_view key, std::string_view from_ms, std::string_view to_ms,
                                 int64_t count = 0);
    std::vector<TsSample> agg(std::string_view key, std::string_view from_ms, std::string_view to_ms,
                               int64_t bucket_ms, std::string_view agg_type);
    TsInfo info(std::string_view key);
    void del(std::string_view key);
    void set_retention(std::string_view key, int64_t retention_ms);
    void downsample(std::string_view src_key, std::string_view dst_key, std::string_view agg_type,
                     int64_t bucket_ms);
    std::vector<std::string> list(std::string_view label = {}, std::string_view value = {});

private:
    Client client_;
};

// FEATURE.* (feature store; license feature: feature-store)
class FeatureStoreCommands {
public:
    explicit FeatureStoreCommands(Client client) : client_(std::move(client)) {}

    void create(std::string_view group, std::string_view description,
                const std::vector<FeatureSchemaField>& schema);
    void set(std::string_view group, std::string_view entity,
             const std::vector<std::pair<std::string, std::string>>& features);
    // Omitting feature_names returns all features for the entity.
    std::optional<FeatureRecord> get(std::string_view group, std::string_view entity,
                                      const std::vector<std::string>& feature_names = {});
    std::vector<std::optional<FeatureRecord>> batch(std::string_view group,
                                                      const std::vector<std::string>& entities,
                                                      const std::vector<std::string>& feature_names = {});
    std::vector<FeatureSchemaField> schema(std::string_view group);
    std::vector<std::string> list();
    void drop(std::string_view group);
    FeatureStats stats(std::string_view group);

private:
    Client client_;
};

// OBSERVE (license feature: observability, admin)
class ObserveCommands {
public:
    explicit ObserveCommands(Client client) : client_(std::move(client)) {}

    ObserveInfo info();
    ObserveMetrics metrics();

private:
    Client client_;
};

// ALERTS (license feature: observability, admin)
class AlertsCommands {
public:
    explicit AlertsCommands(Client client) : client_(std::move(client)) {}

    AlertsInfo info();
    // severity is "info", "warning" or "critical" ("" uses the
    // dispatcher's configured minimum); throws hotkv::ServerError if the
    // test alert is rejected (filtered, rate-limited, deduped, or the
    // queue is full).
    void test(std::string_view severity = {}, std::string_view message = {});
    void set_url(std::string_view url);
    void set_severity(std::string_view severity);
    void on();
    void off();

private:
    Client client_;
};

} // namespace hotkv
