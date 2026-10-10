// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

#include "pairs_util.hpp"
#include "usage_util.hpp"

namespace hotkv {

// --- LlmStatsCommands ---

void LlmStatsCommands::bucket(std::string_view name) {
    client_.command({"LLMSTATS.BUCKET", std::string(name)});
}

void LlmStatsCommands::record(std::string_view bucket, int64_t input_tokens, int64_t output_tokens,
                               int64_t cached_tokens, double cost_usd, int64_t latency_ms,
                               const LlmStatsRecordOptions& opts) {
    std::vector<std::string> args{"LLMSTATS.RECORD",
                                   std::string(bucket),
                                   std::to_string(input_tokens),
                                   std::to_string(output_tokens),
                                   std::to_string(cached_tokens),
                                   detail::format_usd(cost_usd),
                                   std::to_string(latency_ms)};
    if (opts.error.has_value()) {
        args.emplace_back("ERROR");
        args.push_back(*opts.error ? "1" : "0");
    }
    if (!opts.model.empty()) {
        args.emplace_back("MODEL");
        args.push_back(opts.model);
    }
    if (opts.cache_write_5m_tokens > 0) {
        args.emplace_back("CACHEWRITE5M");
        args.push_back(std::to_string(opts.cache_write_5m_tokens));
    }
    if (opts.cache_write_1h_tokens > 0) {
        args.emplace_back("CACHEWRITE1H");
        args.push_back(std::to_string(opts.cache_write_1h_tokens));
    }
    client_.command(std::move(args));
}

LlmStatsSnapshot LlmStatsCommands::get(std::string_view bucket) {
    auto pairs = client_.command({"LLMSTATS.GET", std::string(bucket)}).as_pairs();
    LlmStatsSnapshot s;
    s.requests = detail::pairs_int(pairs, "requests");
    s.errors = detail::pairs_int(pairs, "errors");
    s.input_tokens = detail::pairs_int(pairs, "input_tokens");
    s.output_tokens = detail::pairs_int(pairs, "output_tokens");
    s.cached_tokens = detail::pairs_int(pairs, "cached_tokens");
    s.cost_usd = detail::pairs_double(pairs, "cost_usd");
    s.avg_latency_ms = detail::pairs_double(pairs, "avg_latency_ms");
    s.requests_1m = detail::pairs_int(pairs, "requests_1m");
    s.tokens_1m = detail::pairs_int(pairs, "tokens_1m");
    s.cost_usd_1m = detail::pairs_double(pairs, "cost_usd_1m");
    s.cache_write_5m_tokens = detail::pairs_int(pairs, "cache_write_5m_tokens");
    s.cache_write_1h_tokens = detail::pairs_int(pairs, "cache_write_1h_tokens");
    s.unpriced_requests = detail::pairs_int(pairs, "unpriced_requests");
    return s;
}

bool LlmStatsCommands::reset(std::string_view bucket) {
    return client_.command({"LLMSTATS.RESET", std::string(bucket)}).as_int() != 0;
}

bool LlmStatsCommands::drop(std::string_view bucket) {
    return client_.command({"LLMSTATS.DROP", std::string(bucket)}).as_int() != 0;
}

std::vector<std::string> LlmStatsCommands::list() {
    return client_.command({"LLMSTATS.LIST"}).as_string_array();
}

namespace {
LlmPrice parse_llm_price(const detail::PairList& pairs) {
    LlmPrice p;
    p.model = detail::pairs_string(pairs, "model");
    p.source = detail::pairs_string(pairs, "source");
    p.priced = detail::pairs_bool(pairs, "priced");
    p.retired = detail::pairs_bool(pairs, "retired");
    p.input_usd_per_mtok = detail::pairs_double(pairs, "input_usd_per_mtok");
    p.output_usd_per_mtok = detail::pairs_double(pairs, "output_usd_per_mtok");
    p.cache_read_usd_per_mtok = detail::pairs_double(pairs, "cache_read_usd_per_mtok");
    p.cache_write_5m_usd_per_mtok = detail::pairs_double(pairs, "cache_write_5m_usd_per_mtok");
    p.cache_write_1h_usd_per_mtok = detail::pairs_double(pairs, "cache_write_1h_usd_per_mtok");
    return p;
}
} // namespace

void LlmStatsCommands::price_set(std::string_view model, double input_usd_per_mtok, double output_usd_per_mtok,
                                  const LlmPriceOptions& opts) {
    std::vector<std::string> args{"LLMSTATS.PRICE",
                                   "SET",
                                   std::string(model),
                                   "INPUT",
                                   detail::format_usd(input_usd_per_mtok),
                                   "OUTPUT",
                                   detail::format_usd(output_usd_per_mtok)};
    if (opts.cache_read_usd_per_mtok.has_value()) {
        args.emplace_back("CACHEREAD");
        args.push_back(detail::format_usd(*opts.cache_read_usd_per_mtok));
    }
    if (opts.cache_write_5m_usd_per_mtok.has_value()) {
        args.emplace_back("CACHEWRITE5M");
        args.push_back(detail::format_usd(*opts.cache_write_5m_usd_per_mtok));
    }
    if (opts.cache_write_1h_usd_per_mtok.has_value()) {
        args.emplace_back("CACHEWRITE1H");
        args.push_back(detail::format_usd(*opts.cache_write_1h_usd_per_mtok));
    }
    client_.command(std::move(args));
}

LlmPrice LlmStatsCommands::price_get(std::string_view model) {
    auto pairs = client_.command({"LLMSTATS.PRICE", "GET", std::string(model)}).as_pairs();
    return parse_llm_price(pairs);
}

std::vector<LlmPrice> LlmStatsCommands::price_list() {
    Reply r = client_.command({"LLMSTATS.PRICE", "LIST"});
    std::vector<LlmPrice> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(parse_llm_price(item.as_pairs()));
    }
    return out;
}

bool LlmStatsCommands::price_del(std::string_view model) {
    return client_.command({"LLMSTATS.PRICE", "DEL", std::string(model)}).as_int() != 0;
}

// --- LlmBudgetCommands ---

void LlmBudgetCommands::set(std::string_view name, int64_t token_limit, double cost_limit_usd,
                             int64_t window_seconds) {
    client_.command({"LLMBUDGET.SET", std::string(name), std::to_string(token_limit),
                      detail::format_usd(cost_limit_usd), std::to_string(window_seconds)});
}

bool LlmBudgetCommands::consume(std::string_view name, int64_t tokens, double cost_usd,
                                 const std::optional<Usage>& usage) {
    std::vector<std::string> args{"LLMBUDGET.CONSUME", std::string(name), std::to_string(tokens),
                                   detail::format_usd(cost_usd)};
    detail::append_usage(args, usage);
    return client_.command(std::move(args)).as_int() != 0;
}

LlmBudgetStatus LlmBudgetCommands::status(std::string_view name) {
    auto pairs = client_.command({"LLMBUDGET.STATUS", std::string(name)}).as_pairs();
    LlmBudgetStatus s;
    s.tokens_used = detail::pairs_int(pairs, "tokens_used");
    s.token_limit = detail::pairs_int(pairs, "token_limit");
    s.cost_used_usd = detail::pairs_double(pairs, "cost_used_usd");
    s.cost_limit_usd = detail::pairs_double(pairs, "cost_limit_usd");
    s.window_seconds = detail::pairs_int(pairs, "window_seconds");
    s.window_remaining = detail::pairs_int(pairs, "window_remaining");
    s.requests_allowed = detail::pairs_int(pairs, "requests_allowed");
    s.requests_rejected = detail::pairs_int(pairs, "requests_rejected");
    s.unpriced_requests = detail::pairs_int(pairs, "unpriced_requests");
    return s;
}

bool LlmBudgetCommands::reset(std::string_view name) {
    return client_.command({"LLMBUDGET.RESET", std::string(name)}).as_int() != 0;
}

bool LlmBudgetCommands::drop(std::string_view name) {
    return client_.command({"LLMBUDGET.DROP", std::string(name)}).as_int() != 0;
}

std::vector<std::string> LlmBudgetCommands::list() {
    return client_.command({"LLMBUDGET.LIST"}).as_string_array();
}

// --- PromptRegistryCommands ---

namespace {
std::optional<PromptVersion> parse_prompt_version(const Reply& r) {
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    PromptVersion v;
    v.version = arr.at(0).as_int();
    v.content = arr.at(1).as_string();
    v.metadata = arr.at(2).is_nil() ? std::string() : arr.at(2).as_string();
    v.created_unix = arr.at(3).as_int();
    return v;
}
} // namespace

int64_t PromptRegistryCommands::put(std::string_view name, std::string_view content, std::string_view meta_json) {
    std::vector<std::string> args{"PROMPT.PUT", std::string(name), std::string(content)};
    if (!meta_json.empty()) {
        args.emplace_back("META");
        args.emplace_back(meta_json);
    }
    return client_.command(std::move(args)).as_int();
}

std::optional<PromptVersion> PromptRegistryCommands::get(std::string_view name, int64_t version) {
    std::vector<std::string> args{"PROMPT.GET", std::string(name)};
    if (version != 0) {
        args.push_back(std::to_string(version));
    }
    return parse_prompt_version(client_.command(std::move(args)));
}

std::optional<PromptVersion> PromptRegistryCommands::latest(std::string_view name) {
    return parse_prompt_version(client_.command({"PROMPT.LATEST", std::string(name)}));
}

void PromptRegistryCommands::set_active(std::string_view name, int64_t version) {
    client_.command({"PROMPT.SETACTIVE", std::string(name), std::to_string(version)});
}

std::vector<int64_t> PromptRegistryCommands::versions(std::string_view name) {
    Reply r = client_.command({"PROMPT.VERSIONS", std::string(name)});
    std::vector<int64_t> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_int());
    }
    return out;
}

bool PromptRegistryCommands::drop(std::string_view name) {
    return client_.command({"PROMPT.DROP", std::string(name)}).as_int() != 0;
}

std::vector<std::string> PromptRegistryCommands::list() {
    return client_.command({"PROMPT.LIST"}).as_string_array();
}

// --- EmbedDedupCommands ---

bool EmbedDedupCommands::put(std::string_view ns, std::string_view hash, std::string_view model,
                              std::string_view embedding) {
    return client_
               .command({"EMBED.DEDUP.PUT", std::string(ns), std::string(hash), std::string(model),
                          std::string(embedding)})
               .as_int() != 0;
}

std::optional<std::string> EmbedDedupCommands::get(std::string_view ns, std::string_view hash) {
    return client_.command({"EMBED.DEDUP.GET", std::string(ns), std::string(hash)}).as_optional_string();
}

bool EmbedDedupCommands::release(std::string_view ns, std::string_view hash) {
    return client_.command({"EMBED.DEDUP.RELEASE", std::string(ns), std::string(hash)}).as_int() != 0;
}

EmbedDedupStats EmbedDedupCommands::stats(std::string_view ns) {
    auto pairs = client_.command({"EMBED.DEDUP.STATS", std::string(ns)}).as_pairs();
    EmbedDedupStats s;
    s.hits = detail::pairs_int(pairs, "hits");
    s.misses = detail::pairs_int(pairs, "misses");
    s.entries = detail::pairs_int(pairs, "entries");
    return s;
}

bool EmbedDedupCommands::drop(std::string_view ns) {
    return client_.command({"EMBED.DEDUP.DROP", std::string(ns)}).as_int() != 0;
}

bool EmbedDedupCommands::purge(std::string_view ns) {
    return client_.command({"EMBED.DEDUP.PURGE", std::string(ns)}).as_int() != 0;
}

std::vector<std::string> EmbedDedupCommands::list() {
    return client_.command({"EMBED.DEDUP.LIST"}).as_string_array();
}

} // namespace hotkv
