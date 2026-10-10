// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

// Typed wrappers added after the first release: blocking list/zset pops,
// LMPOP/ZMPOP, GETEX/LCS, newer hash commands, pub/sub publishing,
// OBJECT/EXPIRETIME, and a few server introspection commands.

#include "client_impl.hpp"

#include <hotkv/client.hpp>

namespace hotkv {

namespace {

// A blocking command waits server-side for up to `timeout` (0 = forever);
// the socket must wait at least as long, so extend its read timeout past
// the block. 0 maps to "no socket timeout".
std::chrono::milliseconds socket_timeout_for_block(std::chrono::milliseconds block) {
    if (block.count() <= 0) {
        return std::chrono::milliseconds(0);
    }
    return block + std::chrono::seconds(5);
}

// Seconds as the server expects for BLPOP & co (a decimal, fractions
// allowed): "5", "0.25". Integer arithmetic only, so it is locale free.
std::string seconds_arg(std::chrono::milliseconds ms) {
    int64_t v = ms.count() < 0 ? 0 : ms.count();
    std::string out = std::to_string(v / 1000);
    int64_t frac = v % 1000;
    if (frac != 0) {
        std::string f = std::to_string(frac);
        f.insert(0, 3 - f.size(), '0');
        while (!f.empty() && f.back() == '0') {
            f.pop_back();
        }
        out += "." + f;
    }
    return out;
}

void append_getex(std::vector<std::string>& args, const GetExOptions& o) {
    if (o.ex) {
        args.emplace_back("EX");
        args.push_back(std::to_string(o.ex->count()));
    } else if (o.px) {
        args.emplace_back("PX");
        args.push_back(std::to_string(o.px->count()));
    } else if (o.exat) {
        args.emplace_back("EXAT");
        args.push_back(std::to_string(*o.exat));
    } else if (o.pxat) {
        args.emplace_back("PXAT");
        args.push_back(std::to_string(*o.pxat));
    } else if (o.persist) {
        args.emplace_back("PERSIST");
    }
}

std::vector<std::optional<std::string>> optional_strings(const Reply& r) {
    std::vector<std::optional<std::string>> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_optional_string());
    }
    return out;
}

std::vector<int64_t> int_list(const Reply& r) {
    std::vector<int64_t> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_int());
    }
    return out;
}

std::vector<ScoredMember> scored_list(const Reply& r) {
    std::vector<ScoredMember> out;
    for (auto& pair : r.as_pair_list()) {
        out.push_back(ScoredMember{pair.first.as_string(), pair.second.as_double()});
    }
    return out;
}

std::optional<ListPopResult> list_pop_result(const Reply& r) {
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    ListPopResult res;
    res.key = arr.at(0).as_string();
    res.values = arr.at(1).as_string_array();
    return res;
}

std::optional<ZSetPopResult> zset_pop_result(const Reply& r) {
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    ZSetPopResult res;
    res.key = arr.at(0).as_string();
    res.members = scored_list(arr.at(1));
    return res;
}

std::optional<ZSetBlockingPop> zset_blocking_pop(const Reply& r) {
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    ZSetBlockingPop res;
    res.key = arr.at(0).as_string();
    res.member = arr.at(1).as_string();
    res.score = arr.at(2).as_double();
    return res;
}

std::optional<std::pair<std::string, std::string>> key_value_pair(const Reply& r) {
    if (r.is_nil()) {
        return std::nullopt;
    }
    const Reply::Array& arr = r.as_array();
    return std::make_pair(arr.at(0).as_string(), arr.at(1).as_string());
}

std::vector<std::pair<std::string, int64_t>> name_count_pairs(const Reply& r) {
    // PUBSUB NUMSUB: flat [name, count, name, count ...] (RESP2) or a map (RESP3).
    std::vector<std::pair<std::string, int64_t>> out;
    for (auto& kv : r.as_pairs()) {
        out.emplace_back(kv.first, kv.second.as_int());
    }
    return out;
}

} // namespace

// --- strings ---

std::optional<std::string> Client::getex(std::string_view key, const GetExOptions& opts) {
    std::vector<std::string> args{"GETEX", std::string(key)};
    append_getex(args, opts);
    return command(std::move(args)).as_optional_string();
}

std::string Client::lcs(std::string_view key1, std::string_view key2) {
    return command({"LCS", std::string(key1), std::string(key2)}).as_string();
}

int64_t Client::lcs_len(std::string_view key1, std::string_view key2) {
    return command({"LCS", std::string(key1), std::string(key2), "LEN"}).as_int();
}

// --- keys ---

std::optional<int64_t> Client::object_freq(std::string_view key) {
    return command({"OBJECT", "FREQ", std::string(key)}).as_optional_int();
}

std::optional<int64_t> Client::object_idletime(std::string_view key) {
    return command({"OBJECT", "IDLETIME", std::string(key)}).as_optional_int();
}

std::optional<int64_t> Client::object_refcount(std::string_view key) {
    return command({"OBJECT", "REFCOUNT", std::string(key)}).as_optional_int();
}

int64_t Client::expiretime(std::string_view key) {
    return command({"EXPIRETIME", std::string(key)}).as_int();
}

int64_t Client::pexpiretime(std::string_view key) {
    return command({"PEXPIRETIME", std::string(key)}).as_int();
}

// --- hashes ---

std::vector<int64_t> Client::hexpiretime(std::string_view key, const std::vector<std::string>& fields) {
    std::vector<std::string> args{"HEXPIRETIME", std::string(key), "FIELDS", std::to_string(fields.size())};
    args.insert(args.end(), fields.begin(), fields.end());
    return int_list(command(std::move(args)));
}

std::vector<int64_t> Client::hpexpiretime(std::string_view key, const std::vector<std::string>& fields) {
    std::vector<std::string> args{"HPEXPIRETIME", std::string(key), "FIELDS", std::to_string(fields.size())};
    args.insert(args.end(), fields.begin(), fields.end());
    return int_list(command(std::move(args)));
}

std::vector<std::optional<std::string>> Client::hgetdel(std::string_view key,
                                                         const std::vector<std::string>& fields) {
    std::vector<std::string> args{"HGETDEL", std::string(key), "FIELDS", std::to_string(fields.size())};
    args.insert(args.end(), fields.begin(), fields.end());
    return optional_strings(command(std::move(args)));
}

std::vector<std::optional<std::string>> Client::hgetex(std::string_view key, const std::vector<std::string>& fields,
                                                        const GetExOptions& opts) {
    std::vector<std::string> args{"HGETEX", std::string(key)};
    append_getex(args, opts);
    args.emplace_back("FIELDS");
    args.push_back(std::to_string(fields.size()));
    args.insert(args.end(), fields.begin(), fields.end());
    return optional_strings(command(std::move(args)));
}

bool Client::hsetex(std::string_view key, const std::vector<std::pair<std::string, std::string>>& fields,
                    const HSetExOptions& opts) {
    std::vector<std::string> args{"HSETEX", std::string(key)};
    if (opts.fnx) {
        args.emplace_back("FNX");
    } else if (opts.fxx) {
        args.emplace_back("FXX");
    }
    if (opts.ex) {
        args.emplace_back("EX");
        args.push_back(std::to_string(opts.ex->count()));
    } else if (opts.px) {
        args.emplace_back("PX");
        args.push_back(std::to_string(opts.px->count()));
    } else if (opts.exat) {
        args.emplace_back("EXAT");
        args.push_back(std::to_string(*opts.exat));
    } else if (opts.pxat) {
        args.emplace_back("PXAT");
        args.push_back(std::to_string(*opts.pxat));
    } else if (opts.keepttl) {
        args.emplace_back("KEEPTTL");
    }
    args.emplace_back("FIELDS");
    args.push_back(std::to_string(fields.size()));
    for (const auto& [f, v] : fields) {
        args.push_back(f);
        args.push_back(v);
    }
    return command(std::move(args)).as_int() != 0;
}

// --- lists ---

std::optional<int64_t> Client::lpos(std::string_view key, std::string_view element, std::optional<int64_t> rank,
                                     int64_t maxlen) {
    std::vector<std::string> args{"LPOS", std::string(key), std::string(element)};
    if (rank.has_value()) {
        args.emplace_back("RANK");
        args.push_back(std::to_string(*rank));
    }
    if (maxlen > 0) {
        args.emplace_back("MAXLEN");
        args.push_back(std::to_string(maxlen));
    }
    return command(std::move(args)).as_optional_int();
}

std::optional<ListPopResult> Client::lmpop(const std::vector<std::string>& keys, bool left, int64_t count) {
    std::vector<std::string> args{"LMPOP", std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    args.emplace_back(left ? "LEFT" : "RIGHT");
    args.emplace_back("COUNT");
    args.push_back(std::to_string(count));
    return list_pop_result(command(std::move(args)));
}

std::optional<std::pair<std::string, std::string>> Client::blpop(const std::vector<std::string>& keys,
                                                                  std::chrono::milliseconds timeout) {
    std::vector<std::string> args{"BLPOP"};
    args.insert(args.end(), keys.begin(), keys.end());
    args.push_back(seconds_arg(timeout));
    return key_value_pair(impl_->command(args, socket_timeout_for_block(timeout)));
}

std::optional<std::pair<std::string, std::string>> Client::brpop(const std::vector<std::string>& keys,
                                                                  std::chrono::milliseconds timeout) {
    std::vector<std::string> args{"BRPOP"};
    args.insert(args.end(), keys.begin(), keys.end());
    args.push_back(seconds_arg(timeout));
    return key_value_pair(impl_->command(args, socket_timeout_for_block(timeout)));
}

std::optional<std::string> Client::blmove(std::string_view src, std::string_view dst, bool src_left, bool dst_left,
                                           std::chrono::milliseconds timeout) {
    std::vector<std::string> args{"BLMOVE", std::string(src), std::string(dst), src_left ? "LEFT" : "RIGHT",
                                   dst_left ? "LEFT" : "RIGHT", seconds_arg(timeout)};
    return impl_->command(args, socket_timeout_for_block(timeout)).as_optional_string();
}

std::optional<std::string> Client::brpoplpush(std::string_view src, std::string_view dst,
                                               std::chrono::milliseconds timeout) {
    std::vector<std::string> args{"BRPOPLPUSH", std::string(src), std::string(dst), seconds_arg(timeout)};
    return impl_->command(args, socket_timeout_for_block(timeout)).as_optional_string();
}

std::optional<ListPopResult> Client::blmpop(std::chrono::milliseconds timeout, const std::vector<std::string>& keys,
                                             bool left, int64_t count) {
    std::vector<std::string> args{"BLMPOP", seconds_arg(timeout), std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    args.emplace_back(left ? "LEFT" : "RIGHT");
    args.emplace_back("COUNT");
    args.push_back(std::to_string(count));
    return list_pop_result(impl_->command(args, socket_timeout_for_block(timeout)));
}

// --- sorted sets ---

int64_t Client::zlexcount(std::string_view key, std::string_view min, std::string_view max) {
    return command({"ZLEXCOUNT", std::string(key), std::string(min), std::string(max)}).as_int();
}

std::optional<ZSetPopResult> Client::zmpop(const std::vector<std::string>& keys, bool min, int64_t count) {
    std::vector<std::string> args{"ZMPOP", std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    args.emplace_back(min ? "MIN" : "MAX");
    args.emplace_back("COUNT");
    args.push_back(std::to_string(count));
    return zset_pop_result(command(std::move(args)));
}

std::optional<ZSetBlockingPop> Client::bzpopmin(const std::vector<std::string>& keys,
                                                 std::chrono::milliseconds timeout) {
    std::vector<std::string> args{"BZPOPMIN"};
    args.insert(args.end(), keys.begin(), keys.end());
    args.push_back(seconds_arg(timeout));
    return zset_blocking_pop(impl_->command(args, socket_timeout_for_block(timeout)));
}

std::optional<ZSetBlockingPop> Client::bzpopmax(const std::vector<std::string>& keys,
                                                 std::chrono::milliseconds timeout) {
    std::vector<std::string> args{"BZPOPMAX"};
    args.insert(args.end(), keys.begin(), keys.end());
    args.push_back(seconds_arg(timeout));
    return zset_blocking_pop(impl_->command(args, socket_timeout_for_block(timeout)));
}

std::optional<ZSetPopResult> Client::bzmpop(std::chrono::milliseconds timeout, const std::vector<std::string>& keys,
                                             bool min, int64_t count) {
    std::vector<std::string> args{"BZMPOP", seconds_arg(timeout), std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    args.emplace_back(min ? "MIN" : "MAX");
    args.emplace_back("COUNT");
    args.push_back(std::to_string(count));
    return zset_pop_result(impl_->command(args, socket_timeout_for_block(timeout)));
}

// --- streams ---

std::vector<XPendingEntry> Client::xpending_range(std::string_view key, std::string_view group,
                                                   std::string_view start, std::string_view end, int64_t count,
                                                   std::optional<std::string> consumer,
                                                   std::optional<int64_t> min_idle_ms) {
    std::vector<std::string> args{"XPENDING", std::string(key), std::string(group)};
    if (min_idle_ms.has_value()) {
        args.emplace_back("IDLE");
        args.push_back(std::to_string(*min_idle_ms));
    }
    args.emplace_back(start);
    args.emplace_back(end);
    args.push_back(std::to_string(count));
    if (consumer.has_value()) {
        args.push_back(*consumer);
    }
    Reply r = command(std::move(args));
    std::vector<XPendingEntry> out;
    for (const Reply& row : r.as_array()) {
        const Reply::Array& f = row.as_array();
        XPendingEntry e;
        e.id = f.at(0).as_string();
        e.consumer = f.at(1).as_string();
        e.idle_ms = f.at(2).as_int();
        e.delivery_count = f.at(3).as_int();
        out.push_back(std::move(e));
    }
    return out;
}

// --- bitmaps ---

std::vector<std::optional<int64_t>> Client::bitfield_ro(std::string_view key, const std::vector<std::string>& ops) {
    std::vector<std::string> args{"BITFIELD_RO", std::string(key)};
    args.insert(args.end(), ops.begin(), ops.end());
    Reply r = command(std::move(args));
    std::vector<std::optional<int64_t>> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_optional_int());
    }
    return out;
}

// --- pub/sub (publishing side) ---

int64_t Client::publish(std::string_view channel, std::string_view message) {
    return command({"PUBLISH", std::string(channel), std::string(message)}).as_int();
}

int64_t Client::spublish(std::string_view shard_channel, std::string_view message) {
    return command({"SPUBLISH", std::string(shard_channel), std::string(message)}).as_int();
}

std::vector<std::string> Client::pubsub_channels(std::string_view pattern) {
    std::vector<std::string> args{"PUBSUB", "CHANNELS"};
    if (!pattern.empty()) {
        args.emplace_back(pattern);
    }
    return command(std::move(args)).as_string_array();
}

std::vector<std::pair<std::string, int64_t>> Client::pubsub_numsub(const std::vector<std::string>& channels) {
    std::vector<std::string> args{"PUBSUB", "NUMSUB"};
    args.insert(args.end(), channels.begin(), channels.end());
    return name_count_pairs(command(std::move(args)));
}

int64_t Client::pubsub_numpat() {
    return command({"PUBSUB", "NUMPAT"}).as_int();
}

std::vector<std::string> Client::pubsub_shardchannels(std::string_view pattern) {
    std::vector<std::string> args{"PUBSUB", "SHARDCHANNELS"};
    if (!pattern.empty()) {
        args.emplace_back(pattern);
    }
    return command(std::move(args)).as_string_array();
}

std::vector<std::pair<std::string, int64_t>> Client::pubsub_shardnumsub(const std::vector<std::string>& channels) {
    std::vector<std::string> args{"PUBSUB", "SHARDNUMSUB"};
    args.insert(args.end(), channels.begin(), channels.end());
    return name_count_pairs(command(std::move(args)));
}

// --- server ---

int64_t Client::lastsave() {
    return command({"LASTSAVE"}).as_int();
}

std::optional<int64_t> Client::memory_usage(std::string_view key) {
    return command({"MEMORY", "USAGE", std::string(key)}).as_optional_int();
}

int64_t Client::slowlog_len() {
    return command({"SLOWLOG", "LEN"}).as_int();
}

void Client::slowlog_reset() {
    command({"SLOWLOG", "RESET"});
}

Reply Client::slowlog_get(int64_t count) {
    return command({"SLOWLOG", "GET", std::to_string(count)});
}

void Client::config_resetstat() {
    command({"CONFIG", "RESETSTAT"});
}

void Client::swapdb(int db1, int db2) {
    command({"SWAPDB", std::to_string(db1), std::to_string(db2)});
}

int64_t Client::command_count() {
    return command({"COMMAND", "COUNT"}).as_int();
}

std::string Client::cluster_myid() {
    return command({"CLUSTER", "MYID"}).as_string();
}

std::string Client::cluster_info() {
    return command({"CLUSTER", "INFO"}).as_string();
}

std::string Client::cluster_nodes() {
    return command({"CLUSTER", "NODES"}).as_string();
}

Reply Client::cluster_slots() {
    return command({"CLUSTER", "SLOTS"});
}

int64_t Client::cluster_keyslot(std::string_view key) {
    return command({"CLUSTER", "KEYSLOT", std::string(key)}).as_int();
}

int64_t Client::cluster_countkeysinslot(int slot) {
    return command({"CLUSTER", "COUNTKEYSINSLOT", std::to_string(slot)}).as_int();
}

std::vector<std::string> Client::cluster_getkeysinslot(int slot, int64_t count) {
    return command({"CLUSTER", "GETKEYSINSLOT", std::to_string(slot), std::to_string(count)}).as_string_array();
}

} // namespace hotkv
