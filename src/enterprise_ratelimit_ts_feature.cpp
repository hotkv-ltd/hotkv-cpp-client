// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

#include "pairs_util.hpp"

namespace hotkv {

// --- RateLimitCommands ---

namespace {
RateLimitCheckResult parse_ratelimit_result(const Reply& r) {
    const Reply::Array& arr = r.as_array();
    RateLimitCheckResult res;
    if (arr.size() >= 5) {
        res.allowed = arr[0].as_int() != 0;
        res.remaining = arr[1].as_int();
        res.retry_after_ms = arr[2].as_int();
        res.limit = arr[3].as_int();
        res.current = arr[4].as_int();
    }
    return res;
}
} // namespace

void RateLimitCommands::create(std::string_view name, int64_t limit, int64_t window_ms,
                                const RateLimitCreateOptions& opts) {
    std::vector<std::string> args{"RATELIMIT.CREATE", std::string(name), std::to_string(limit),
                                   std::to_string(window_ms)};
    if (!opts.algo.empty()) {
        args.emplace_back("ALGO");
        args.push_back(opts.algo);
    }
    if (opts.refill.has_value()) {
        args.emplace_back("REFILL");
        args.push_back(detail::format_finite(*opts.refill));
    }
    if (opts.drain.has_value()) {
        args.emplace_back("DRAIN");
        args.push_back(detail::format_finite(*opts.drain));
    }
    client_.command(std::move(args));
}

RateLimitCheckResult RateLimitCommands::check(std::string_view name, std::string_view key, int64_t cost) {
    std::vector<std::string> args{"RATELIMIT.CHECK", std::string(name), std::string(key)};
    if (cost != 1) {
        args.emplace_back("COST");
        args.push_back(std::to_string(cost));
    }
    return parse_ratelimit_result(client_.command(std::move(args)));
}

RateLimitCheckResult RateLimitCommands::peek(std::string_view name, std::string_view key) {
    return parse_ratelimit_result(client_.command({"RATELIMIT.PEEK", std::string(name), std::string(key)}));
}

void RateLimitCommands::reset(std::string_view name, std::string_view key) {
    client_.command({"RATELIMIT.RESET", std::string(name), std::string(key)});
}

RateLimitStats RateLimitCommands::stats(std::string_view name) {
    auto pairs = client_.command({"RATELIMIT.STATS", std::string(name)}).as_pairs();
    RateLimitStats s;
    s.name = detail::pairs_string(pairs, "name");
    s.algo = detail::pairs_string(pairs, "algo");
    s.limit = detail::pairs_int(pairs, "limit");
    s.window_ms = detail::pairs_int(pairs, "window_ms");
    s.unique_keys = detail::pairs_int(pairs, "unique_keys");
    s.total_allowed = detail::pairs_int(pairs, "total_allowed");
    s.total_denied = detail::pairs_int(pairs, "total_denied");
    return s;
}

void RateLimitCommands::drop(std::string_view name) {
    client_.command({"RATELIMIT.DROP", std::string(name)});
}

std::vector<std::string> RateLimitCommands::list() {
    return client_.command({"RATELIMIT.LIST"}).as_string_array();
}

// --- TimeSeriesCommands ---

namespace {
std::vector<TsSample> parse_ts_samples(const Reply& r) {
    std::vector<TsSample> out;
    for (const Reply& item : r.as_array()) {
        const Reply::Array& pair = item.as_array();
        out.push_back(TsSample{pair.at(0).as_int(), pair.at(1).as_double()});
    }
    return out;
}
} // namespace

void TimeSeriesCommands::create(std::string_view key, const TsCreateOptions& opts) {
    std::vector<std::string> args{"TS.CREATE", std::string(key)};
    if (opts.retention_ms.has_value()) {
        args.emplace_back("RETENTION");
        args.push_back(std::to_string(*opts.retention_ms));
    }
    if (!opts.labels.empty()) {
        args.emplace_back("LABELS");
        for (const auto& [k, v] : opts.labels) {
            args.push_back(k);
            args.push_back(v);
        }
    }
    client_.command(std::move(args));
}

int64_t TimeSeriesCommands::add(std::string_view key, std::optional<int64_t> timestamp_ms, double value) {
    std::string ts = timestamp_ms.has_value() ? std::to_string(*timestamp_ms) : "*";
    return client_.command({"TS.ADD", std::string(key), ts, detail::format_double(value)}).as_int();
}

std::vector<TsSample> TimeSeriesCommands::range(std::string_view key, std::string_view from_ms,
                                                  std::string_view to_ms, int64_t count) {
    std::vector<std::string> args{"TS.RANGE", std::string(key), std::string(from_ms), std::string(to_ms)};
    if (count > 0) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(count));
    }
    return parse_ts_samples(client_.command(std::move(args)));
}

std::vector<TsSample> TimeSeriesCommands::agg(std::string_view key, std::string_view from_ms, std::string_view to_ms,
                                                int64_t bucket_ms, std::string_view agg_type) {
    return parse_ts_samples(client_.command({"TS.AGG", std::string(key), std::string(from_ms), std::string(to_ms),
                                              std::to_string(bucket_ms), std::string(agg_type)}));
}

TsInfo TimeSeriesCommands::info(std::string_view key) {
    auto pairs = client_.command({"TS.INFO", std::string(key)}).as_pairs();
    TsInfo out;
    out.key = detail::pairs_string(pairs, "key");
    out.total_samples = detail::pairs_int(pairs, "totalSamples");
    out.first_timestamp = detail::pairs_int(pairs, "firstTimestamp");
    out.last_timestamp = detail::pairs_int(pairs, "lastTimestamp");
    out.last_value = detail::pairs_double(pairs, "lastValue");
    out.retention_ms = detail::pairs_int(pairs, "retentionMs");
    out.memory_bytes = detail::pairs_int(pairs, "memoryBytes");
    if (const Reply* labels = detail::pairs_find(pairs, "labels"); labels != nullptr && !labels->is_nil()) {
        for (const Reply& item : labels->as_array()) {
            const Reply::Array& pair = item.as_array();
            out.labels.emplace_back(pair.at(0).as_string(), pair.at(1).as_string());
        }
    }
    out.rules = detail::pairs_int(pairs, "rules");
    return out;
}

void TimeSeriesCommands::del(std::string_view key) {
    client_.command({"TS.DELETE", std::string(key)});
}

void TimeSeriesCommands::set_retention(std::string_view key, int64_t retention_ms) {
    client_.command({"TS.RETENTION", std::string(key), std::to_string(retention_ms)});
}

void TimeSeriesCommands::downsample(std::string_view src_key, std::string_view dst_key, std::string_view agg_type,
                                     int64_t bucket_ms) {
    client_.command(
        {"TS.DOWNSAMPLE", std::string(src_key), std::string(dst_key), std::string(agg_type), std::to_string(bucket_ms)});
}

std::vector<std::string> TimeSeriesCommands::list(std::string_view label, std::string_view value) {
    std::vector<std::string> args{"TS.LIST"};
    if (!label.empty()) {
        args.emplace_back("LABEL");
        args.emplace_back(label);
        args.emplace_back(value);
    }
    return client_.command(std::move(args)).as_string_array();
}

// --- FeatureStoreCommands ---

namespace {

// FEATURE.GET/BATCH reply each feature's value pre-formatted per its
// schema type: int64 fields come back as a native RESP3 integer, double
// and string fields as a bulk string, so a plain as_string() would throw
// on an int64 field. This renders any scalar reply as text (and falls
// back to a debug rendering for the array feature types, which this
// SDK's string-valued FeatureRecord does not otherwise model).
std::string feature_value_to_string(const Reply& r) {
    if (r.is_nil()) {
        return {};
    }
    switch (r.type()) {
        case ReplyType::BulkString:
        case ReplyType::SimpleString:
        case ReplyType::Verbatim:
        case ReplyType::BigNumber:
            return r.as_string();
        case ReplyType::Integer:
            return std::to_string(r.as_int());
        case ReplyType::Double:
            return detail::format_lossless(r.as_double());
        case ReplyType::Boolean:
            return r.as_bool() ? "1" : "0";
        default:
            return r.debug_string();
    }
}

std::optional<FeatureRecord> parse_feature_record(const Reply& r) {
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    FeatureRecord rec;
    for (auto& kv : arr.at(0).as_pairs()) {
        rec.values.emplace_back(kv.first, feature_value_to_string(kv.second));
    }
    rec.updated_at_ms = arr.at(1).as_int();
    rec.freshness_ms = arr.at(2).as_int();
    return rec;
}
} // namespace

void FeatureStoreCommands::create(std::string_view group, std::string_view description,
                                   const std::vector<FeatureSchemaField>& schema) {
    std::vector<std::string> args{"FEATURE.CREATE", std::string(group), std::string(description), "SCHEMA"};
    for (const auto& f : schema) {
        args.push_back(f.name);
        args.push_back(f.type);
    }
    client_.command(std::move(args));
}

void FeatureStoreCommands::set(std::string_view group, std::string_view entity,
                                const std::vector<std::pair<std::string, std::string>>& features) {
    std::vector<std::string> args{"FEATURE.SET", std::string(group), std::string(entity)};
    for (const auto& [k, v] : features) {
        args.push_back(k);
        args.push_back(v);
    }
    client_.command(std::move(args));
}

std::optional<FeatureRecord> FeatureStoreCommands::get(std::string_view group, std::string_view entity,
                                                         const std::vector<std::string>& feature_names) {
    std::vector<std::string> args{"FEATURE.GET", std::string(group), std::string(entity)};
    args.insert(args.end(), feature_names.begin(), feature_names.end());
    return parse_feature_record(client_.command(std::move(args)));
}

std::vector<std::optional<FeatureRecord>> FeatureStoreCommands::batch(std::string_view group,
                                                                        const std::vector<std::string>& entities,
                                                                        const std::vector<std::string>& feature_names) {
    std::vector<std::string> args{"FEATURE.BATCH", std::string(group)};
    args.insert(args.end(), entities.begin(), entities.end());
    if (!feature_names.empty()) {
        args.emplace_back("FEATURES");
        args.insert(args.end(), feature_names.begin(), feature_names.end());
    }
    Reply r = client_.command(std::move(args));
    std::vector<std::optional<FeatureRecord>> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(parse_feature_record(item));
    }
    return out;
}

std::vector<FeatureSchemaField> FeatureStoreCommands::schema(std::string_view group) {
    Reply r = client_.command({"FEATURE.SCHEMA", std::string(group)});
    std::vector<FeatureSchemaField> out;
    for (const Reply& item : r.as_array()) {
        const Reply::Array& pair = item.as_array();
        out.push_back(FeatureSchemaField{pair.at(0).as_string(), pair.at(1).as_string()});
    }
    return out;
}

std::vector<std::string> FeatureStoreCommands::list() {
    return client_.command({"FEATURE.LIST"}).as_string_array();
}

void FeatureStoreCommands::drop(std::string_view group) {
    client_.command({"FEATURE.DROP", std::string(group)});
}

FeatureStats FeatureStoreCommands::stats(std::string_view group) {
    auto pairs = client_.command({"FEATURE.STATS", std::string(group)}).as_pairs();
    FeatureStats s;
    s.entities = detail::pairs_int(pairs, "entities");
    s.total_reads = detail::pairs_int(pairs, "totalReads");
    s.total_writes = detail::pairs_int(pairs, "totalWrites");
    s.memory_bytes = detail::pairs_int(pairs, "memoryBytes");
    return s;
}

} // namespace hotkv
