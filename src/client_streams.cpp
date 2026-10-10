// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

#include "client_impl.hpp"

namespace hotkv {

namespace {

StreamEntry parse_stream_entry(const Reply& r) {
    const Reply::Array& pair = r.as_array();
    StreamEntry entry;
    if (!pair.empty()) {
        entry.id = pair[0].as_string();
    }
    if (pair.size() >= 2 && !pair[1].is_nil()) {
        const Reply::Array& flat = pair[1].as_array();
        for (std::size_t i = 0; i + 1 < flat.size(); i += 2) {
            entry.fields.emplace_back(flat[i].as_string(), flat[i + 1].as_string());
        }
    }
    return entry;
}

std::vector<StreamEntry> parse_stream_entries(const Reply& r) {
    std::vector<StreamEntry> out;
    if (r.is_nil()) {
        return out;
    }
    for (const Reply& item : r.as_array()) {
        out.push_back(parse_stream_entry(item));
    }
    return out;
}

// Decodes XREAD/XREADGROUP's reply, which is a RESP2 array of [stream,
// entries] pairs or, under RESP3, a native map of stream -> entries.
std::vector<XReadResult> parse_xread(const Reply& r) {
    std::vector<XReadResult> out;
    if (r.is_nil()) {
        return out;
    }
    if (r.type() == ReplyType::Map) {
        for (const auto& kv : r.as_map()) {
            out.push_back(XReadResult{kv.first.as_string(), parse_stream_entries(kv.second)});
        }
        return out;
    }
    for (const Reply& pair : r.as_array()) {
        const Reply::Array& kv = pair.as_array();
        out.push_back(XReadResult{kv[0].as_string(), parse_stream_entries(kv[1])});
    }
    return out;
}

} // namespace

std::string Client::xadd(std::string_view key, const std::vector<std::pair<std::string, std::string>>& fields,
                          std::string_view id) {
    std::vector<std::string> args{"XADD", std::string(key), std::string(id)};
    for (const auto& [f, v] : fields) {
        args.push_back(f);
        args.push_back(v);
    }
    return command(std::move(args)).as_string();
}

std::string Client::xadd_maxlen(std::string_view key, int64_t maxlen, bool approx,
                                 const std::vector<std::pair<std::string, std::string>>& fields,
                                 std::string_view id) {
    std::vector<std::string> args{"XADD", std::string(key), "MAXLEN", approx ? "~" : "=", std::to_string(maxlen),
                                   std::string(id)};
    for (const auto& [f, v] : fields) {
        args.push_back(f);
        args.push_back(v);
    }
    return command(std::move(args)).as_string();
}

int64_t Client::xlen(std::string_view key) {
    return command({"XLEN", std::string(key)}).as_int();
}

std::vector<StreamEntry> Client::xrange(std::string_view key, std::string_view start, std::string_view end,
                                         int64_t count) {
    std::vector<std::string> args{"XRANGE", std::string(key), std::string(start), std::string(end)};
    if (count > 0) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(count));
    }
    return parse_stream_entries(command(std::move(args)));
}

std::vector<StreamEntry> Client::xrevrange(std::string_view key, std::string_view end, std::string_view start,
                                            int64_t count) {
    std::vector<std::string> args{"XREVRANGE", std::string(key), std::string(end), std::string(start)};
    if (count > 0) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(count));
    }
    return parse_stream_entries(command(std::move(args)));
}

int64_t Client::xdel(std::string_view key, const std::vector<std::string>& ids) {
    std::vector<std::string> args{"XDEL", std::string(key)};
    args.insert(args.end(), ids.begin(), ids.end());
    return command(std::move(args)).as_int();
}

int64_t Client::xtrim_maxlen(std::string_view key, int64_t maxlen, bool approx) {
    return command({"XTRIM", std::string(key), "MAXLEN", approx ? "~" : "=", std::to_string(maxlen)}).as_int();
}

std::vector<XReadResult> Client::xread(const std::vector<std::pair<std::string, std::string>>& streams,
                                        int64_t count, std::optional<int64_t> block_ms) {
    std::vector<std::string> args{"XREAD"};
    if (count > 0) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(count));
    }
    if (block_ms.has_value()) {
        args.emplace_back("BLOCK");
        args.push_back(std::to_string(*block_ms));
    }
    args.emplace_back("STREAMS");
    for (const auto& entry : streams) {
        args.push_back(entry.first);
    }
    for (const auto& entry : streams) {
        args.push_back(entry.second);
    }
    std::optional<std::chrono::milliseconds> timeout_override;
    if (block_ms.has_value()) {
        // BLOCK 0 waits forever: no socket timeout either.
        timeout_override = *block_ms == 0 ? std::chrono::milliseconds(0)
                                          : std::chrono::milliseconds(*block_ms) + std::chrono::seconds(5);
    }
    return parse_xread(impl_->command(args, timeout_override));
}

std::vector<XReadResult> Client::xreadgroup(std::string_view group, std::string_view consumer,
                                             const std::vector<std::pair<std::string, std::string>>& streams,
                                             int64_t count, std::optional<int64_t> block_ms, bool noack) {
    std::vector<std::string> args{"XREADGROUP", "GROUP", std::string(group), std::string(consumer)};
    if (count > 0) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(count));
    }
    if (block_ms.has_value()) {
        args.emplace_back("BLOCK");
        args.push_back(std::to_string(*block_ms));
    }
    if (noack) {
        args.emplace_back("NOACK");
    }
    args.emplace_back("STREAMS");
    for (const auto& entry : streams) {
        args.push_back(entry.first);
    }
    for (const auto& entry : streams) {
        args.push_back(entry.second);
    }
    std::optional<std::chrono::milliseconds> timeout_override;
    if (block_ms.has_value()) {
        // BLOCK 0 waits forever: no socket timeout either.
        timeout_override = *block_ms == 0 ? std::chrono::milliseconds(0)
                                          : std::chrono::milliseconds(*block_ms) + std::chrono::seconds(5);
    }
    return parse_xread(impl_->command(args, timeout_override));
}

void Client::xgroup_create(std::string_view key, std::string_view group, std::string_view id, bool mkstream) {
    std::vector<std::string> args{"XGROUP", "CREATE", std::string(key), std::string(group), std::string(id)};
    if (mkstream) {
        args.emplace_back("MKSTREAM");
    }
    command(std::move(args));
}

bool Client::xgroup_destroy(std::string_view key, std::string_view group) {
    return command({"XGROUP", "DESTROY", std::string(key), std::string(group)}).as_int() != 0;
}

bool Client::xgroup_createconsumer(std::string_view key, std::string_view group, std::string_view consumer) {
    return command({"XGROUP", "CREATECONSUMER", std::string(key), std::string(group), std::string(consumer)})
               .as_int() != 0;
}

int64_t Client::xgroup_delconsumer(std::string_view key, std::string_view group, std::string_view consumer) {
    return command({"XGROUP", "DELCONSUMER", std::string(key), std::string(group), std::string(consumer)}).as_int();
}

void Client::xgroup_setid(std::string_view key, std::string_view group, std::string_view id) {
    command({"XGROUP", "SETID", std::string(key), std::string(group), std::string(id)});
}

int64_t Client::xack(std::string_view key, std::string_view group, const std::vector<std::string>& ids) {
    std::vector<std::string> args{"XACK", std::string(key), std::string(group)};
    args.insert(args.end(), ids.begin(), ids.end());
    return command(std::move(args)).as_int();
}

XPendingSummary Client::xpending(std::string_view key, std::string_view group) {
    Reply r = command({"XPENDING", std::string(key), std::string(group)});
    const Reply::Array& arr = r.as_array();
    XPendingSummary summary;
    if (arr.size() >= 4) {
        summary.count = arr[0].as_int();
        summary.min_id = arr[1].as_optional_string();
        summary.max_id = arr[2].as_optional_string();
        if (!arr[3].is_nil()) {
            for (const Reply& c : arr[3].as_array()) {
                const Reply::Array& pair = c.as_array();
                summary.consumers.emplace_back(pair[0].as_string(), pair[1].as_int());
            }
        }
    }
    return summary;
}

std::vector<StreamEntry> Client::xclaim(std::string_view key, std::string_view group, std::string_view consumer,
                                         int64_t min_idle_ms, const std::vector<std::string>& ids) {
    std::vector<std::string> args{"XCLAIM", std::string(key), std::string(group), std::string(consumer),
                                   std::to_string(min_idle_ms)};
    args.insert(args.end(), ids.begin(), ids.end());
    return parse_stream_entries(command(std::move(args)));
}

XAutoclaimResult Client::xautoclaim(std::string_view key, std::string_view group, std::string_view consumer,
                                     int64_t min_idle_ms, std::string_view start, int64_t count) {
    std::vector<std::string> args{"XAUTOCLAIM", std::string(key), std::string(group), std::string(consumer),
                                   std::to_string(min_idle_ms), std::string(start)};
    if (count > 0) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(count));
    }
    Reply r = command(std::move(args));
    const Reply::Array& arr = r.as_array();
    XAutoclaimResult result;
    if (!arr.empty()) {
        result.cursor = arr[0].as_string();
    }
    if (arr.size() >= 2) {
        result.entries = parse_stream_entries(arr[1]);
    }
    if (arr.size() >= 3) {
        result.deleted_ids = arr[2].as_string_array();
    }
    return result;
}

Reply Client::xinfo_stream(std::string_view key) {
    return command({"XINFO", "STREAM", std::string(key)});
}

Reply Client::xinfo_groups(std::string_view key) {
    return command({"XINFO", "GROUPS", std::string(key)});
}

Reply Client::xinfo_consumers(std::string_view key, std::string_view group) {
    return command({"XINFO", "CONSUMERS", std::string(key), std::string(group)});
}

} // namespace hotkv
