// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hotkv {

// Conditional flag shared by EXPIRE/PEXPIRE/EXPIREAT/PEXPIREAT and the
// HEXPIRE family: None sends no condition keyword.
enum class ExpireCondition { None, Nx, Xx, Gt, Lt };

// Optional flags for Client::set(). Combine ex/px/exat/pxat (choose at
// most one) with nx/xx/keepttl as needed.
struct SetOptions {
    std::optional<std::chrono::seconds> ex;
    std::optional<std::chrono::milliseconds> px;
    std::optional<int64_t> exat; // absolute unix time, seconds
    std::optional<int64_t> pxat; // absolute unix time, milliseconds
    bool nx = false;
    bool xx = false;
    bool keepttl = false;
};

enum class ZRangeBy { Index, Score, Lex };

// Options for Client::zrange()/zrange_withscores()/zrangestore(). `by`
// selects whether start/stop are 0-based indexes, scores, or lex bounds
// ("[a", "(z", "-", "+").
struct ZRangeOptions {
    ZRangeBy by = ZRangeBy::Index;
    bool rev = false;
    std::optional<int64_t> limit_offset;
    std::optional<int64_t> limit_count;
};

// Flags for Client::zadd(). At most one of nx/xx and at most one of
// gt/lt should be set, matching ZADD's own rules.
struct ZAddOptions {
    bool nx = false;
    bool xx = false;
    bool gt = false;
    bool lt = false;
    bool ch = false;
};

// WEIGHTS/AGGREGATE for ZUNIONSTORE/ZINTERSTORE/ZUNION/ZINTER.
struct ZStoreOptions {
    std::vector<double> weights;
    std::string aggregate; // "" (server default SUM), "SUM", "MIN" or "MAX"
};

// MATCH/COUNT/TYPE for the SCAN family (TYPE applies to SCAN only).
struct ScanOptions {
    std::string match;
    int64_t count = 0;
    std::string type;
};

struct ScoredMember {
    std::string member;
    double score = 0.0;
};

struct StreamEntry {
    std::string id;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct XReadResult {
    std::string stream;
    std::vector<StreamEntry> entries;
};

struct XPendingSummary {
    int64_t count = 0;
    std::optional<std::string> min_id;
    std::optional<std::string> max_id;
    // (consumer name, pending count) pairs.
    std::vector<std::pair<std::string, int64_t>> consumers;
};

struct XAutoclaimResult {
    std::string cursor;
    std::vector<StreamEntry> entries;
    std::vector<std::string> deleted_ids;
};

struct GeoPoint {
    double longitude = 0.0;
    double latitude = 0.0;
};

enum class GeoUnit { Meters, Kilometers, Miles, Feet };

// Origin and shape for Client::geosearch()/geosearchstore(): set exactly
// one of from_member/from_lonlat, and exactly one of radius/box.
struct GeoSearchOptions {
    std::optional<std::string> from_member;
    std::optional<GeoPoint> from_lonlat;
    std::optional<double> radius; // BYRADIUS, in `unit`
    std::optional<std::pair<double, double>> box; // BYBOX: width, height, in `unit`
    GeoUnit unit = GeoUnit::Meters;
    bool asc = false;
    bool desc = false;
    std::optional<int64_t> count;
    bool count_any = false;
    bool with_coord = false;
    bool with_dist = false;
    bool with_hash = false;
};

struct GeoSearchResult {
    std::string member;
    std::optional<double> distance;
    std::optional<GeoPoint> coord;
    std::optional<int64_t> geohash;
};

// GETEX / HGETEX expiry choice: set at most one of ex/px/exat/pxat/persist;
// none set reads without touching the TTL.
struct GetExOptions {
    std::optional<std::chrono::seconds> ex;
    std::optional<std::chrono::milliseconds> px;
    std::optional<int64_t> exat; // absolute unix time, seconds
    std::optional<int64_t> pxat; // absolute unix time, milliseconds
    bool persist = false;
};

// HSETEX flags: fnx (only if none of the fields exist) or fxx (only if
// all exist), and at most one of ex/px/exat/pxat/keepttl.
struct HSetExOptions {
    bool fnx = false;
    bool fxx = false;
    std::optional<std::chrono::seconds> ex;
    std::optional<std::chrono::milliseconds> px;
    std::optional<int64_t> exat;
    std::optional<int64_t> pxat;
    bool keepttl = false;
};

// Result of LMPOP / BLMPOP: the key that was popped from and its values.
struct ListPopResult {
    std::string key;
    std::vector<std::string> values;
};

// Result of ZMPOP / BZMPOP.
struct ZSetPopResult {
    std::string key;
    std::vector<ScoredMember> members;
};

// Result of BZPOPMIN / BZPOPMAX.
struct ZSetBlockingPop {
    std::string key;
    std::string member;
    double score = 0.0;
};

// One row of the extended XPENDING form.
struct XPendingEntry {
    std::string id;
    std::string consumer;
    int64_t idle_ms = 0;
    int64_t delivery_count = 0;
};

struct ServerTime {
    int64_t seconds = 0;
    int64_t microseconds = 0;
};

} // namespace hotkv
