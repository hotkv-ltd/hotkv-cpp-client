// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hotkv {

// The RESP2/RESP3 wire type a Reply actually holds. RESP2-only
// connections never produce Boolean, Double, BigNumber, Verbatim, Map,
// Set or Push; those appear once HELLO 3 negotiates RESP3 (Client
// negotiates RESP3 by default and falls back automatically).
enum class ReplyType {
    Null,
    Boolean,
    Integer,
    Double,
    BigNumber,
    SimpleString,
    BulkString,
    Verbatim,
    Error,
    Array,
    Map,
    Set,
    Push,
};

// A single RESP2/RESP3 reply value, covering every wire type. Container
// replies (Array, Map, Set, Push) nest further Reply values. Reply is a
// plain value type: copyable, movable, safe to store.
//
// Typed accessors below convert to the expected C++ type and throw
// hotkv::ReplyTypeError if the reply is not a compatible kind. A nil
// reply is never an error: use is_nil() or the `optional`-returning
// accessors to handle it without a try/catch.
class Reply {
public:
    using Array = std::vector<Reply>;
    using MapEntries = std::vector<std::pair<Reply, Reply>>;

    Reply() = default;

    static Reply make_null();
    static Reply make_boolean(bool value);
    static Reply make_integer(int64_t value);
    static Reply make_double(double value);
    static Reply make_big_number(std::string digits);
    static Reply make_simple_string(std::string value);
    static Reply make_bulk_string(std::string value);
    static Reply make_verbatim(std::string format, std::string value);
    static Reply make_error(std::string message);
    static Reply make_array(Array elements);
    static Reply make_map(MapEntries entries);
    static Reply make_set(Array elements);
    static Reply make_push(Array elements);

    ReplyType type() const noexcept { return type_; }
    bool is_nil() const noexcept { return type_ == ReplyType::Null; }
    bool is_error() const noexcept { return type_ == ReplyType::Error; }
    // True for Array, Set and Push: the three "sequence of Reply" shapes.
    bool is_array_like() const noexcept;

    // RESP3 attributes (`|...`) that preceded this reply on the wire, if
    // the server sent any. Almost always empty; exposed for completeness.
    const std::optional<MapEntries>& attributes() const noexcept { return attributes_; }
    void set_attributes(MapEntries attrs) { attributes_ = std::move(attrs); }

    // --- error ---

    // The raw error message, e.g. "WRONGTYPE Operation against a key...".
    // Throws ReplyTypeError if this is not an error reply.
    const std::string& error_message() const;
    // The leading token of the error message (e.g. "WRONGTYPE"), or the
    // empty string if the message has no leading token. Throws
    // ReplyTypeError if this is not an error reply.
    std::string error_code() const;
    // Throws the specific hotkv::ServerError subclass matching this
    // error's code (see error.hpp). No-op if this is not an error reply.
    void raise_if_error() const;

    // --- scalars ---

    bool as_bool() const;
    int64_t as_int() const;
    double as_double() const;
    // Bulk/simple/verbatim/big-number replies as a binary safe string.
    const std::string& as_string() const;
    // Two-letter content-type tag of a Verbatim reply ("txt" or "mkd").
    // Throws ReplyTypeError if this is not a Verbatim reply.
    const std::string& verbatim_format() const;

    std::optional<std::string> as_optional_string() const;
    std::optional<int64_t> as_optional_int() const;
    std::optional<double> as_optional_double() const;

    // --- containers ---

    const Array& as_array() const;
    const MapEntries& as_map() const;

    // Converts an Array/Set/Push reply of bulk/simple strings into a
    // vector<string>; skips the accessor ceremony for the very common
    // "array of strings" shape (TAG.GET, KEYS, SMEMBERS, ...).
    std::vector<std::string> as_string_array() const;

    // Converts either a native RESP3 Map reply, or a RESP2-style flat
    // array reply (`name, value, name, value, ...`), into a uniform list
    // of (string key, Reply value) pairs. Most HotKV commands document a
    // "flat array of N pairs" reply; this accessor decodes that shape
    // whether the wire form is a flat Array or a native Map, so callers
    // do not need to branch on the negotiated protocol version.
    std::vector<std::pair<std::string, Reply>> as_pairs() const;

    // Converts a "list of (a, b) pairs" reply into a uniform list of
    // (Reply, Reply) pairs, whether the wire form is a RESP2-style flat
    // array (`a, b, a, b, ...`) or a RESP3 array of 2-element sub-arrays
    // (`[a, b], [a, b], ...`), which several commands (ZRANGE
    // WITHSCORES, HRANDFIELD WITHVALUES, ZRANDMEMBER WITHSCORES, ...)
    // switch to under RESP3. Unlike as_pairs(), neither side is coerced
    // to a string, since both a and b are typically meaningful values
    // here (e.g. member and score).
    std::vector<std::pair<Reply, Reply>> as_pair_list() const;

    // Human readable rendering for logs and exception messages; not a
    // wire format.
    std::string debug_string() const;

private:
    ReplyType type_ = ReplyType::Null;
    bool bool_value_ = false;
    int64_t int_value_ = 0;
    double double_value_ = 0.0;
    std::string string_value_;    // simple/bulk/verbatim/big-number/error text
    std::string verbatim_format_; // "txt" / "mkd", Verbatim only
    Array array_value_;           // Array / Set / Push
    MapEntries map_value_;        // Map
    std::optional<MapEntries> attributes_;
};

} // namespace hotkv
