// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "client_impl.hpp"

#include <hotkv/error.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace hotkv::detail {

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

const std::unordered_set<std::string>& readonly_commands() {
    static const std::unordered_set<std::string> cmds = {
        // core
        "GET", "MGET", "STRLEN", "GETRANGE", "EXISTS", "TYPE", "TTL", "PTTL", "KEYS", "RANDOMKEY", "DUMP", "TOUCH",
        "OBJECT", "LLEN", "LRANGE", "LINDEX", "SMEMBERS", "SISMEMBER", "SMISMEMBER", "SCARD", "SRANDMEMBER",
        "SINTER", "SINTERCARD", "SUNION", "SDIFF", "HGET", "HMGET", "HGETALL", "HKEYS", "HVALS", "HLEN", "HEXISTS",
        "HSTRLEN", "HRANDFIELD", "HTTL", "HPTTL", "ZSCORE", "ZMSCORE", "ZRANK", "ZREVRANK", "ZCARD", "ZCOUNT",
        "ZRANGE", "ZRANGEBYSCORE", "ZREVRANGEBYSCORE", "ZRANGEBYLEX", "ZREVRANGEBYLEX", "ZRANDMEMBER", "ZUNION",
        "ZINTER", "ZDIFF", "ZINTERCARD", "XRANGE", "XREVRANGE", "XLEN", "XPENDING", "GEOPOS", "GEODIST", "GEOHASH",
        "GEOSEARCH", "GETBIT", "BITCOUNT", "BITPOS", "PFCOUNT", "SCAN", "HSCAN", "SSCAN", "ZSCAN", "PING", "ECHO",
        "TIME", "DBSIZE", "INFO", "LASTSAVE", "NTTL",
        // HotKV / AI, dotted single-token names
        "TAG.GET", "TAG.MEMBERS", "TAG.INTER", "TAG.COUNT", "TAG.EXISTS", "VSIM.GET", "VSIM.SEARCH", "VSIM.INFO",
        "VSIM.LIST", "SCACHE.GET", "SCACHE.STATS", "SCACHE.LIST", "PCACHE.GET",
        "PCACHE.GETEX", "PCACHE.STATS", "PCACHE.LIST", "TOOL.CACHE.GET", "TOOL.CACHE.STATS", "TOOL.CACHE.LIST",
        "NCACHE.GET", "NCACHE.STATS", "NCACHE.LIST", "GUARD.GET", "GUARD.STATS", "GUARD.LIST", "RERANK.GET",
        "RERANK.STATS", "RERANK.LIST", "LLMSTATS.GET", "LLMSTATS.LIST", "LLMBUDGET.STATUS", "LLMBUDGET.LIST",
        "PROMPT.GET", "PROMPT.LATEST", "PROMPT.VERSIONS", "PROMPT.LIST", "EMBED.DEDUP.GET", "EMBED.DEDUP.STATS",
        "EMBED.DEDUP.LIST", "AMEM.CONV.GET", "AMEM.CONV.WINDOW", "AMEM.SEM.RECALL", "AMEM.SEM.LIST",
        "AMEM.WORK.GET", "AMEM.WORK.LIST", "AMEM.EPIS.QUERY", "AMEM.EPIS.STATS", "AMEM.SEARCH",
        "AMEM.SUMMARY.GET", "AMEM.SUMMARY.LIST", "RAG.SEARCH", "RAG.STATS", "RAG.LIST", "RATELIMIT.PEEK",
        "RATELIMIT.STATS", "RATELIMIT.LIST", "TS.RANGE", "TS.AGG", "TS.INFO", "TS.LIST", "FEATURE.GET",
        "FEATURE.BATCH", "FEATURE.SCHEMA", "FEATURE.LIST", "FEATURE.STATS",
    };
    return cmds;
}

// "NAME SUB" pairs for two-word commands whose first word alone would be
// ambiguous (CONFIG SET is a write, CONFIG GET is a read; and so on).
const std::unordered_set<std::string>& readonly_subcommand_pairs() {
    static const std::unordered_set<std::string> cmds = {
        "CONFIG GET",  "CLIENT GETNAME", "CLIENT ID",      "CLIENT LIST",     "CLIENT INFO",
        "CLUSTER SLOTS", "CLUSTER SHARDS", "CLUSTER NODES", "CLUSTER KEYSLOT", "CLUSTER INFO",
        "OBSERVE INFO", "OBSERVE METRICS", "ALERTS INFO",   "XINFO STREAM",    "XINFO GROUPS",
        "XINFO CONSUMERS", "SCRIPT EXISTS", "HOTKEYS TOP",
    };
    return cmds;
}

} // namespace

bool is_idempotent_read(const std::vector<std::string>& args) {
    if (args.empty()) {
        return false;
    }
    std::string name = upper(args[0]);
    if (readonly_commands().count(name) != 0) {
        return true;
    }
    if (args.size() >= 2 && readonly_subcommand_pairs().count(name + " " + upper(args[1])) != 0) {
        return true;
    }
    return false;
}

Reply ClientImpl::command(const std::vector<std::string>& args, std::optional<std::chrono::milliseconds> timeout_override) {
    bool retryable = is_idempotent_read(args);
    for (int attempt = 0;; ++attempt) {
        ConnectionPool::Lease lease = pool_.acquire();
        try {
            Reply reply = lease->send_command(args, timeout_override);
            reply.raise_if_error();
            return reply;
        } catch (const ServerError&) {
            // A server-level error is not a transport failure: the
            // connection stays healthy (Lease returns it on destruction)
            // and the error is never retried.
            throw;
        } catch (const ConnectionError&) {
            lease.mark_broken();
            if (retryable && attempt == 0) {
                continue;
            }
            throw;
        } catch (...) {
            lease.mark_broken();
            throw;
        }
    }
}

} // namespace hotkv::detail
