// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

#include "pairs_util.hpp"

namespace hotkv {

// --- AmemConvCommands ---

void AmemConvCommands::create(std::string_view session_id, std::optional<int64_t> window,
                               std::optional<int64_t> ttl_seconds) {
    std::vector<std::string> args{"AMEM.CONV.CREATE", std::string(session_id)};
    if (window.has_value()) {
        args.emplace_back("WINDOW");
        args.push_back(std::to_string(*window));
    }
    if (ttl_seconds.has_value()) {
        args.emplace_back("TTL");
        args.push_back(std::to_string(*ttl_seconds));
    }
    client_.command(std::move(args));
}

void AmemConvCommands::append(std::string_view session_id, std::string_view role, std::string_view content) {
    client_.command({"AMEM.CONV.APPEND", std::string(session_id), std::string(role), std::string(content)});
}

namespace {
std::vector<AmemTurn> parse_turns(const Reply& r) {
    std::vector<AmemTurn> out;
    for (const Reply& item : r.as_array()) {
        auto pairs = item.as_pairs();
        out.push_back(AmemTurn{detail::pairs_string(pairs, "role"), detail::pairs_string(pairs, "content")});
    }
    return out;
}
} // namespace

std::vector<AmemTurn> AmemConvCommands::get(std::string_view session_id) {
    return parse_turns(client_.command({"AMEM.CONV.GET", std::string(session_id)}));
}

std::vector<AmemTurn> AmemConvCommands::window(std::string_view session_id, int64_t n) {
    return parse_turns(client_.command({"AMEM.CONV.WINDOW", std::string(session_id), std::to_string(n)}));
}

bool AmemConvCommands::clear(std::string_view session_id) {
    return client_.command({"AMEM.CONV.CLEAR", std::string(session_id)}).as_int() != 0;
}

bool AmemConvCommands::drop(std::string_view session_id) {
    return client_.command({"AMEM.CONV.DROP", std::string(session_id)}).as_int() != 0;
}

// --- AmemSemCommands ---

void AmemSemCommands::create(std::string_view agent_id, int64_t dimensions) {
    client_.command({"AMEM.SEM.CREATE", std::string(agent_id), std::to_string(dimensions)});
}

void AmemSemCommands::store(std::string_view agent_id, std::string_view key, std::string_view content,
                             const std::vector<double>& vector, std::optional<int64_t> ttl_seconds) {
    std::vector<std::string> args{"AMEM.SEM.STORE", std::string(agent_id), std::string(key), std::string(content)};
    for (double v : vector) {
        args.push_back(detail::format_finite(v));
    }
    if (ttl_seconds.has_value()) {
        args.emplace_back("TTL");
        args.push_back(std::to_string(*ttl_seconds));
    }
    client_.command(std::move(args));
}

std::vector<AmemRecallResult> AmemSemCommands::recall(std::string_view agent_id, int64_t k,
                                                        const std::vector<double>& vector) {
    std::vector<std::string> args{"AMEM.SEM.RECALL", std::string(agent_id), std::to_string(k)};
    for (double v : vector) {
        args.push_back(detail::format_finite(v));
    }
    Reply r = client_.command(std::move(args));
    std::vector<AmemRecallResult> out;
    for (const Reply& item : r.as_array()) {
        auto pairs = item.as_pairs();
        out.push_back(AmemRecallResult{detail::pairs_string(pairs, "key"), detail::pairs_string(pairs, "content"),
                                        detail::pairs_double(pairs, "similarity")});
    }
    return out;
}

bool AmemSemCommands::forget(std::string_view agent_id, std::string_view key) {
    return client_.command({"AMEM.SEM.FORGET", std::string(agent_id), std::string(key)}).as_int() != 0;
}

std::vector<std::string> AmemSemCommands::list(std::string_view agent_id) {
    return client_.command({"AMEM.SEM.LIST", std::string(agent_id)}).as_string_array();
}

// --- AmemWorkCommands ---

void AmemWorkCommands::set(std::string_view session_id, std::string_view key, std::string_view value,
                            std::optional<int64_t> ttl_seconds) {
    std::vector<std::string> args{"AMEM.WORK.SET", std::string(session_id), std::string(key), std::string(value)};
    if (ttl_seconds.has_value()) {
        args.emplace_back("TTL");
        args.push_back(std::to_string(*ttl_seconds));
    }
    client_.command(std::move(args));
}

std::optional<std::string> AmemWorkCommands::get(std::string_view session_id, std::string_view key) {
    return client_.command({"AMEM.WORK.GET", std::string(session_id), std::string(key)}).as_optional_string();
}

bool AmemWorkCommands::del(std::string_view session_id, std::string_view key) {
    return client_.command({"AMEM.WORK.DEL", std::string(session_id), std::string(key)}).as_int() != 0;
}

bool AmemWorkCommands::clear(std::string_view session_id) {
    return client_.command({"AMEM.WORK.CLEAR", std::string(session_id)}).as_int() != 0;
}

std::vector<std::string> AmemWorkCommands::list(std::string_view session_id) {
    return client_.command({"AMEM.WORK.LIST", std::string(session_id)}).as_string_array();
}

// --- AmemEpisCommands ---

void AmemEpisCommands::log(std::string_view agent_id, std::string_view event_type, std::string_view content,
                            std::string_view metadata_json) {
    std::vector<std::string> args{"AMEM.EPIS.LOG", std::string(agent_id), std::string(event_type),
                                   std::string(content)};
    if (!metadata_json.empty()) {
        args.emplace_back("METADATA");
        args.emplace_back(metadata_json);
    }
    client_.command(std::move(args));
}

std::vector<AmemEpisodicEvent> AmemEpisCommands::query(std::string_view agent_id,
                                                         const AmemEpisQueryOptions& opts) {
    std::vector<std::string> args{"AMEM.EPIS.QUERY", std::string(agent_id)};
    if (!opts.type.empty()) {
        args.emplace_back("TYPE");
        args.push_back(opts.type);
    }
    if (opts.from_ts.has_value()) {
        args.emplace_back("FROM");
        args.push_back(std::to_string(*opts.from_ts));
    }
    if (opts.to_ts.has_value()) {
        args.emplace_back("TO");
        args.push_back(std::to_string(*opts.to_ts));
    }
    if (opts.limit.has_value()) {
        args.emplace_back("LIMIT");
        args.push_back(std::to_string(*opts.limit));
    }
    Reply r = client_.command(std::move(args));
    std::vector<AmemEpisodicEvent> out;
    for (const Reply& item : r.as_array()) {
        auto pairs = item.as_pairs();
        AmemEpisodicEvent e;
        e.type = detail::pairs_string(pairs, "type");
        e.content = detail::pairs_string(pairs, "content");
        e.metadata = detail::pairs_string(pairs, "metadata");
        e.timestamp_ns = detail::pairs_int(pairs, "timestamp");
        out.push_back(std::move(e));
    }
    return out;
}

bool AmemEpisCommands::clear(std::string_view agent_id) {
    return client_.command({"AMEM.EPIS.CLEAR", std::string(agent_id)}).as_int() != 0;
}

AmemEpisodicStats AmemEpisCommands::stats(std::string_view agent_id) {
    auto pairs = client_.command({"AMEM.EPIS.STATS", std::string(agent_id)}).as_pairs();
    AmemEpisodicStats s;
    s.total_events = detail::pairs_int(pairs, "total_events");
    s.oldest_ts = detail::pairs_int(pairs, "oldest_ts");
    s.newest_ts = detail::pairs_int(pairs, "newest_ts");
    return s;
}

// --- AmemSummaryCommands ---

int64_t AmemSummaryCommands::set(std::string_view agent_id, std::string_view summary) {
    return client_.command({"AMEM.SUMMARY.SET", std::string(agent_id), std::string(summary)}).as_int();
}

int64_t AmemSummaryCommands::append(std::string_view agent_id, std::string_view extra) {
    return client_.command({"AMEM.SUMMARY.APPEND", std::string(agent_id), std::string(extra)}).as_int();
}

std::optional<AmemSummary> AmemSummaryCommands::get(std::string_view agent_id) {
    Reply r = client_.command({"AMEM.SUMMARY.GET", std::string(agent_id)});
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    AmemSummary s;
    s.summary = arr.at(0).as_string();
    s.version = arr.at(1).as_int();
    s.updated_unix = arr.at(2).as_int();
    return s;
}

bool AmemSummaryCommands::drop(std::string_view agent_id) {
    return client_.command({"AMEM.SUMMARY.DROP", std::string(agent_id)}).as_int() != 0;
}

std::vector<std::string> AmemSummaryCommands::list() {
    return client_.command({"AMEM.SUMMARY.LIST"}).as_string_array();
}

// --- AgentMemoryCommands::search (cross-memory search) ---

std::vector<AmemSearchResult> AgentMemoryCommands::search(std::string_view agent_id, int64_t k,
                                                            const std::vector<double>& vector) {
    std::vector<std::string> args{"AMEM.SEARCH", std::string(agent_id), std::to_string(k)};
    for (double v : vector) {
        args.push_back(detail::format_finite(v));
    }
    Reply r = client_.command(std::move(args));
    std::vector<AmemSearchResult> out;
    for (const Reply& item : r.as_array()) {
        auto pairs = item.as_pairs();
        AmemSearchResult res;
        res.source = detail::pairs_string(pairs, "source");
        res.key = detail::pairs_string(pairs, "key");
        res.content = detail::pairs_string(pairs, "content");
        res.similarity = detail::pairs_double(pairs, "similarity");
        out.push_back(std::move(res));
    }
    return out;
}

} // namespace hotkv
