// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

namespace {

std::vector<ScoredMember> as_scored_members(const Reply& r) {
    std::vector<ScoredMember> out;
    // WITHSCORES replies switch from a flat array to an array of
    // 2-element sub-arrays under RESP3; as_pair_list() decodes either.
    // The score side may itself be a native RESP3 double or a bulk
    // string, both handled by Reply::as_double().
    for (auto& pair : r.as_pair_list()) {
        out.push_back(ScoredMember{pair.first.as_string(), pair.second.as_double()});
    }
    return out;
}

void append_range_by(std::vector<std::string>& args, ZRangeBy by) {
    if (by == ZRangeBy::Score) {
        args.emplace_back("BYSCORE");
    } else if (by == ZRangeBy::Lex) {
        args.emplace_back("BYLEX");
    }
}

void append_limit(std::vector<std::string>& args, std::optional<int64_t> offset, std::optional<int64_t> count) {
    if (offset.has_value() || count.has_value()) {
        args.emplace_back("LIMIT");
        args.push_back(std::to_string(offset.value_or(0)));
        args.push_back(std::to_string(count.value_or(-1)));
    }
}

void append_store_options(std::vector<std::string>& args, const ZStoreOptions& opts) {
    if (!opts.weights.empty()) {
        args.emplace_back("WEIGHTS");
        for (double w : opts.weights) {
            args.push_back(detail::format_double(w));
        }
    }
    if (!opts.aggregate.empty()) {
        args.emplace_back("AGGREGATE");
        args.push_back(opts.aggregate);
    }
}

std::vector<std::string> keys_with_count_prefix(const char* cmd, const std::vector<std::string>& keys) {
    std::vector<std::string> args{cmd, std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    return args;
}

} // namespace

int64_t Client::zadd(std::string_view key, const std::vector<ScoredMember>& members, const ZAddOptions& opts) {
    std::vector<std::string> args{"ZADD", std::string(key)};
    if (opts.nx) {
        args.emplace_back("NX");
    } else if (opts.xx) {
        args.emplace_back("XX");
    }
    if (opts.gt) {
        args.emplace_back("GT");
    } else if (opts.lt) {
        args.emplace_back("LT");
    }
    if (opts.ch) {
        args.emplace_back("CH");
    }
    for (const auto& m : members) {
        args.push_back(detail::format_double(m.score));
        args.push_back(m.member);
    }
    return command(std::move(args)).as_int();
}

std::optional<double> Client::zadd_incr(std::string_view key, double delta, std::string_view member,
                                         const ZAddOptions& opts) {
    std::vector<std::string> args{"ZADD", std::string(key)};
    if (opts.nx) {
        args.emplace_back("NX");
    } else if (opts.xx) {
        args.emplace_back("XX");
    }
    if (opts.gt) {
        args.emplace_back("GT");
    } else if (opts.lt) {
        args.emplace_back("LT");
    }
    args.emplace_back("INCR");
    args.push_back(detail::format_double(delta));
    args.emplace_back(member);
    return command(std::move(args)).as_optional_double();
}

int64_t Client::zrem(std::string_view key, const std::vector<std::string>& members) {
    std::vector<std::string> args{"ZREM", std::string(key)};
    args.insert(args.end(), members.begin(), members.end());
    return command(std::move(args)).as_int();
}

std::optional<double> Client::zscore(std::string_view key, std::string_view member) {
    return command({"ZSCORE", std::string(key), std::string(member)}).as_optional_double();
}

std::vector<std::optional<double>> Client::zmscore(std::string_view key, const std::vector<std::string>& members) {
    std::vector<std::string> args{"ZMSCORE", std::string(key)};
    args.insert(args.end(), members.begin(), members.end());
    Reply r = command(std::move(args));
    std::vector<std::optional<double>> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_optional_double());
    }
    return out;
}

std::optional<int64_t> Client::zrank(std::string_view key, std::string_view member) {
    return command({"ZRANK", std::string(key), std::string(member)}).as_optional_int();
}

std::optional<int64_t> Client::zrevrank(std::string_view key, std::string_view member) {
    return command({"ZREVRANK", std::string(key), std::string(member)}).as_optional_int();
}

int64_t Client::zcard(std::string_view key) {
    return command({"ZCARD", std::string(key)}).as_int();
}

int64_t Client::zcount(std::string_view key, std::string_view min, std::string_view max) {
    return command({"ZCOUNT", std::string(key), std::string(min), std::string(max)}).as_int();
}

double Client::zincrby(std::string_view key, double delta, std::string_view member) {
    return command({"ZINCRBY", std::string(key), detail::format_double(delta), std::string(member)}).as_double();
}

std::vector<std::string> Client::zrange(std::string_view key, std::string_view start, std::string_view stop,
                                         const ZRangeOptions& opts) {
    std::vector<std::string> args{"ZRANGE", std::string(key), std::string(start), std::string(stop)};
    append_range_by(args, opts.by);
    if (opts.rev) {
        args.emplace_back("REV");
    }
    append_limit(args, opts.limit_offset, opts.limit_count);
    return command(std::move(args)).as_string_array();
}

std::vector<ScoredMember> Client::zrange_withscores(std::string_view key, std::string_view start,
                                                     std::string_view stop, const ZRangeOptions& opts) {
    std::vector<std::string> args{"ZRANGE", std::string(key), std::string(start), std::string(stop)};
    append_range_by(args, opts.by);
    if (opts.rev) {
        args.emplace_back("REV");
    }
    append_limit(args, opts.limit_offset, opts.limit_count);
    args.emplace_back("WITHSCORES");
    return as_scored_members(command(std::move(args)));
}

int64_t Client::zrangestore(std::string_view dst, std::string_view src, std::string_view start,
                             std::string_view stop, const ZRangeOptions& opts) {
    std::vector<std::string> args{"ZRANGESTORE", std::string(dst), std::string(src), std::string(start),
                                   std::string(stop)};
    append_range_by(args, opts.by);
    if (opts.rev) {
        args.emplace_back("REV");
    }
    append_limit(args, opts.limit_offset, opts.limit_count);
    return command(std::move(args)).as_int();
}

std::vector<std::string> Client::zrangebyscore(std::string_view key, std::string_view min, std::string_view max,
                                                std::optional<int64_t> limit_offset,
                                                std::optional<int64_t> limit_count) {
    std::vector<std::string> args{"ZRANGEBYSCORE", std::string(key), std::string(min), std::string(max)};
    append_limit(args, limit_offset, limit_count);
    return command(std::move(args)).as_string_array();
}

std::vector<ScoredMember> Client::zrangebyscore_withscores(std::string_view key, std::string_view min,
                                                            std::string_view max,
                                                            std::optional<int64_t> limit_offset,
                                                            std::optional<int64_t> limit_count) {
    std::vector<std::string> args{"ZRANGEBYSCORE", std::string(key), std::string(min), std::string(max)};
    append_limit(args, limit_offset, limit_count);
    args.emplace_back("WITHSCORES");
    return as_scored_members(command(std::move(args)));
}

std::vector<std::string> Client::zrevrangebyscore(std::string_view key, std::string_view max, std::string_view min,
                                                   std::optional<int64_t> limit_offset,
                                                   std::optional<int64_t> limit_count) {
    std::vector<std::string> args{"ZREVRANGEBYSCORE", std::string(key), std::string(max), std::string(min)};
    append_limit(args, limit_offset, limit_count);
    return command(std::move(args)).as_string_array();
}

std::vector<ScoredMember> Client::zrevrangebyscore_withscores(std::string_view key, std::string_view max,
                                                               std::string_view min,
                                                               std::optional<int64_t> limit_offset,
                                                               std::optional<int64_t> limit_count) {
    std::vector<std::string> args{"ZREVRANGEBYSCORE", std::string(key), std::string(max), std::string(min)};
    append_limit(args, limit_offset, limit_count);
    args.emplace_back("WITHSCORES");
    return as_scored_members(command(std::move(args)));
}

std::vector<std::string> Client::zrangebylex(std::string_view key, std::string_view min, std::string_view max,
                                              std::optional<int64_t> limit_offset,
                                              std::optional<int64_t> limit_count) {
    std::vector<std::string> args{"ZRANGEBYLEX", std::string(key), std::string(min), std::string(max)};
    append_limit(args, limit_offset, limit_count);
    return command(std::move(args)).as_string_array();
}

std::vector<std::string> Client::zrevrangebylex(std::string_view key, std::string_view max, std::string_view min,
                                                 std::optional<int64_t> limit_offset,
                                                 std::optional<int64_t> limit_count) {
    std::vector<std::string> args{"ZREVRANGEBYLEX", std::string(key), std::string(max), std::string(min)};
    append_limit(args, limit_offset, limit_count);
    return command(std::move(args)).as_string_array();
}

int64_t Client::zremrangebyrank(std::string_view key, int64_t start, int64_t stop) {
    return command({"ZREMRANGEBYRANK", std::string(key), std::to_string(start), std::to_string(stop)}).as_int();
}

int64_t Client::zremrangebyscore(std::string_view key, std::string_view min, std::string_view max) {
    return command({"ZREMRANGEBYSCORE", std::string(key), std::string(min), std::string(max)}).as_int();
}

int64_t Client::zremrangebylex(std::string_view key, std::string_view min, std::string_view max) {
    return command({"ZREMRANGEBYLEX", std::string(key), std::string(min), std::string(max)}).as_int();
}

std::vector<ScoredMember> Client::zpopmin(std::string_view key, int64_t count) {
    return as_scored_members(command({"ZPOPMIN", std::string(key), std::to_string(count)}));
}

std::vector<ScoredMember> Client::zpopmax(std::string_view key, int64_t count) {
    return as_scored_members(command({"ZPOPMAX", std::string(key), std::to_string(count)}));
}

std::vector<std::string> Client::zrandmember(std::string_view key, int64_t count) {
    return command({"ZRANDMEMBER", std::string(key), std::to_string(count)}).as_string_array();
}

std::vector<ScoredMember> Client::zrandmember_withscores(std::string_view key, int64_t count) {
    return as_scored_members(command({"ZRANDMEMBER", std::string(key), std::to_string(count), "WITHSCORES"}));
}

int64_t Client::zunionstore(std::string_view dst, const std::vector<std::string>& keys, const ZStoreOptions& opts) {
    std::vector<std::string> args{"ZUNIONSTORE", std::string(dst), std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    append_store_options(args, opts);
    return command(std::move(args)).as_int();
}

int64_t Client::zinterstore(std::string_view dst, const std::vector<std::string>& keys, const ZStoreOptions& opts) {
    std::vector<std::string> args{"ZINTERSTORE", std::string(dst), std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    append_store_options(args, opts);
    return command(std::move(args)).as_int();
}

int64_t Client::zdiffstore(std::string_view dst, const std::vector<std::string>& keys) {
    std::vector<std::string> args{"ZDIFFSTORE", std::string(dst), std::to_string(keys.size())};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

std::vector<std::string> Client::zunion(const std::vector<std::string>& keys, const ZStoreOptions& opts) {
    std::vector<std::string> args = keys_with_count_prefix("ZUNION", keys);
    append_store_options(args, opts);
    return command(std::move(args)).as_string_array();
}

std::vector<ScoredMember> Client::zunion_withscores(const std::vector<std::string>& keys,
                                                     const ZStoreOptions& opts) {
    std::vector<std::string> args = keys_with_count_prefix("ZUNION", keys);
    append_store_options(args, opts);
    args.emplace_back("WITHSCORES");
    return as_scored_members(command(std::move(args)));
}

std::vector<std::string> Client::zinter(const std::vector<std::string>& keys, const ZStoreOptions& opts) {
    std::vector<std::string> args = keys_with_count_prefix("ZINTER", keys);
    append_store_options(args, opts);
    return command(std::move(args)).as_string_array();
}

std::vector<ScoredMember> Client::zinter_withscores(const std::vector<std::string>& keys,
                                                     const ZStoreOptions& opts) {
    std::vector<std::string> args = keys_with_count_prefix("ZINTER", keys);
    append_store_options(args, opts);
    args.emplace_back("WITHSCORES");
    return as_scored_members(command(std::move(args)));
}

int64_t Client::zintercard(const std::vector<std::string>& keys, int64_t limit) {
    std::vector<std::string> args = keys_with_count_prefix("ZINTERCARD", keys);
    if (limit > 0) {
        args.emplace_back("LIMIT");
        args.push_back(std::to_string(limit));
    }
    return command(std::move(args)).as_int();
}

std::vector<std::string> Client::zdiff(const std::vector<std::string>& keys) {
    return command(keys_with_count_prefix("ZDIFF", keys)).as_string_array();
}

std::vector<ScoredMember> Client::zdiff_withscores(const std::vector<std::string>& keys) {
    std::vector<std::string> args = keys_with_count_prefix("ZDIFF", keys);
    args.emplace_back("WITHSCORES");
    return as_scored_members(command(std::move(args)));
}

} // namespace hotkv
