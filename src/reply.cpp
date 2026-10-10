// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/reply.hpp>

#include <hotkv/detail/numfmt.hpp>

#include <hotkv/error.hpp>

namespace hotkv {

Reply Reply::make_null() {
    Reply r;
    r.type_ = ReplyType::Null;
    return r;
}

Reply Reply::make_boolean(bool value) {
    Reply r;
    r.type_ = ReplyType::Boolean;
    r.bool_value_ = value;
    return r;
}

Reply Reply::make_integer(int64_t value) {
    Reply r;
    r.type_ = ReplyType::Integer;
    r.int_value_ = value;
    return r;
}

Reply Reply::make_double(double value) {
    Reply r;
    r.type_ = ReplyType::Double;
    r.double_value_ = value;
    return r;
}

Reply Reply::make_big_number(std::string digits) {
    Reply r;
    r.type_ = ReplyType::BigNumber;
    r.string_value_ = std::move(digits);
    return r;
}

Reply Reply::make_simple_string(std::string value) {
    Reply r;
    r.type_ = ReplyType::SimpleString;
    r.string_value_ = std::move(value);
    return r;
}

Reply Reply::make_bulk_string(std::string value) {
    Reply r;
    r.type_ = ReplyType::BulkString;
    r.string_value_ = std::move(value);
    return r;
}

Reply Reply::make_verbatim(std::string format, std::string value) {
    Reply r;
    r.type_ = ReplyType::Verbatim;
    r.verbatim_format_ = std::move(format);
    r.string_value_ = std::move(value);
    return r;
}

Reply Reply::make_error(std::string message) {
    Reply r;
    r.type_ = ReplyType::Error;
    r.string_value_ = std::move(message);
    return r;
}

Reply Reply::make_array(Array elements) {
    Reply r;
    r.type_ = ReplyType::Array;
    r.array_value_ = std::move(elements);
    return r;
}

Reply Reply::make_map(MapEntries entries) {
    Reply r;
    r.type_ = ReplyType::Map;
    r.map_value_ = std::move(entries);
    return r;
}

Reply Reply::make_set(Array elements) {
    Reply r;
    r.type_ = ReplyType::Set;
    r.array_value_ = std::move(elements);
    return r;
}

Reply Reply::make_push(Array elements) {
    Reply r;
    r.type_ = ReplyType::Push;
    r.array_value_ = std::move(elements);
    return r;
}

bool Reply::is_array_like() const noexcept {
    return type_ == ReplyType::Array || type_ == ReplyType::Set || type_ == ReplyType::Push;
}

namespace {
[[noreturn]] void fail(const char* wanted, ReplyType got) {
    static const char* names[] = {"Null",   "Boolean", "Integer", "Double", "BigNumber", "SimpleString",
                                   "BulkString", "Verbatim", "Error", "Array", "Map", "Set", "Push"};
    std::string msg = "hotkv: reply is not ";
    msg += wanted;
    msg += " (actual type: ";
    msg += names[static_cast<int>(got)];
    msg += ")";
    throw ReplyTypeError(msg);
}
} // namespace

const std::string& Reply::error_message() const {
    if (type_ != ReplyType::Error) {
        fail("an error", type_);
    }
    return string_value_;
}

std::string Reply::error_code() const {
    const std::string& msg = error_message();
    auto pos = msg.find(' ');
    return pos == std::string::npos ? msg : msg.substr(0, pos);
}

void Reply::raise_if_error() const {
    if (type_ == ReplyType::Error) {
        throw_server_error(string_value_);
    }
}

bool Reply::as_bool() const {
    switch (type_) {
        case ReplyType::Boolean:
            return bool_value_;
        case ReplyType::Integer:
            return int_value_ != 0;
        default:
            fail("a boolean", type_);
    }
}

int64_t Reply::as_int() const {
    switch (type_) {
        case ReplyType::Integer:
            return int_value_;
        case ReplyType::Boolean:
            return bool_value_ ? 1 : 0;
        case ReplyType::Double:
            return static_cast<int64_t>(double_value_);
        case ReplyType::BulkString:
        case ReplyType::SimpleString:
        case ReplyType::Verbatim:
        case ReplyType::BigNumber: {
            try {
                return std::stoll(string_value_);
            } catch (const std::exception&) {
                fail("an integer", type_);
            }
        }
        default:
            fail("an integer", type_);
    }
}

double Reply::as_double() const {
    switch (type_) {
        case ReplyType::Double:
            return double_value_;
        case ReplyType::Integer:
            return static_cast<double>(int_value_);
        case ReplyType::BulkString:
        case ReplyType::SimpleString:
        case ReplyType::Verbatim:
        case ReplyType::BigNumber: {
            double parsed = 0.0;
            if (!detail::parse_double(string_value_, parsed)) {
                fail("a double", type_);
            }
            return parsed;
        }
        default:
            fail("a double", type_);
    }
}

const std::string& Reply::as_string() const {
    switch (type_) {
        case ReplyType::BulkString:
        case ReplyType::SimpleString:
        case ReplyType::Verbatim:
        case ReplyType::BigNumber:
            return string_value_;
        default:
            fail("a string", type_);
    }
}

const std::string& Reply::verbatim_format() const {
    if (type_ != ReplyType::Verbatim) {
        fail("a verbatim string", type_);
    }
    return verbatim_format_;
}

std::optional<std::string> Reply::as_optional_string() const {
    if (is_nil()) {
        return std::nullopt;
    }
    return as_string();
}

std::optional<int64_t> Reply::as_optional_int() const {
    if (is_nil()) {
        return std::nullopt;
    }
    return as_int();
}

std::optional<double> Reply::as_optional_double() const {
    if (is_nil()) {
        return std::nullopt;
    }
    return as_double();
}

const Reply::Array& Reply::as_array() const {
    if (!is_array_like()) {
        fail("an array", type_);
    }
    return array_value_;
}

const Reply::MapEntries& Reply::as_map() const {
    if (type_ != ReplyType::Map) {
        fail("a map", type_);
    }
    return map_value_;
}

std::vector<std::string> Reply::as_string_array() const {
    std::vector<std::string> out;
    const Array& arr = as_array();
    out.reserve(arr.size());
    for (const Reply& item : arr) {
        out.push_back(item.is_nil() ? std::string() : item.as_string());
    }
    return out;
}

std::vector<std::pair<Reply, Reply>> Reply::as_pair_list() const {
    const Array& arr = as_array();
    std::vector<std::pair<Reply, Reply>> out;
    if (!arr.empty() && arr[0].is_array_like() && arr[0].as_array().size() == 2) {
        // RESP3 array-of-pairs shape: [[a, b], [a, b], ...].
        out.reserve(arr.size());
        for (const Reply& item : arr) {
            const Array& pair = item.as_array();
            out.emplace_back(pair[0], pair[1]);
        }
        return out;
    }
    // RESP2-style flat shape: [a, b, a, b, ...].
    out.reserve(arr.size() / 2);
    for (std::size_t i = 0; i + 1 < arr.size(); i += 2) {
        out.emplace_back(arr[i], arr[i + 1]);
    }
    return out;
}

std::vector<std::pair<std::string, Reply>> Reply::as_pairs() const {
    std::vector<std::pair<std::string, Reply>> out;
    if (type_ == ReplyType::Map) {
        out.reserve(map_value_.size());
        for (const auto& kv : map_value_) {
            out.emplace_back(kv.first.as_string(), kv.second);
        }
        return out;
    }
    const Array& arr = as_array();
    out.reserve(arr.size() / 2);
    for (std::size_t i = 0; i + 1 < arr.size(); i += 2) {
        out.emplace_back(arr[i].as_string(), arr[i + 1]);
    }
    return out;
}

std::string Reply::debug_string() const {
    switch (type_) {
        case ReplyType::Null:
            return "(nil)";
        case ReplyType::Boolean:
            return bool_value_ ? "true" : "false";
        case ReplyType::Integer:
            return std::to_string(int_value_);
        case ReplyType::Double:
            return detail::format_lossless(double_value_);
        case ReplyType::BigNumber:
            return string_value_;
        case ReplyType::SimpleString:
            return string_value_;
        case ReplyType::BulkString:
            return "\"" + string_value_ + "\"";
        case ReplyType::Verbatim:
            return verbatim_format_ + ":" + string_value_;
        case ReplyType::Error:
            return "(error) " + string_value_;
        case ReplyType::Array:
        case ReplyType::Set:
        case ReplyType::Push: {
            std::string out = "[";
            for (std::size_t i = 0; i < array_value_.size(); ++i) {
                if (i != 0) {
                    out += ", ";
                }
                out += array_value_[i].debug_string();
            }
            out += "]";
            return out;
        }
        case ReplyType::Map: {
            std::string out = "{";
            for (std::size_t i = 0; i < map_value_.size(); ++i) {
                if (i != 0) {
                    out += ", ";
                }
                out += map_value_[i].first.debug_string() + ": " + map_value_[i].second.debug_string();
            }
            out += "}";
            return out;
        }
    }
    return "(unknown)";
}

} // namespace hotkv
