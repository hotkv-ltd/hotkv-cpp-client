// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/detail/numfmt.hpp>

#include <string>
#include <string_view>
#include <type_traits>

namespace hotkv::detail {

// Converts one argument of Client::execute(...) to a command token.
// Strings pass through unchanged (execute() never reformats them, since
// callers who need exact numeric formatting should format themselves);
// integers use std::to_string, and floating point values use the
// locale-independent shortest round-trip form of hotkv::detail::format_double
// (NaN throws hotkv::InvalidArgumentError).
inline std::string to_arg(std::string_view value) {
    return std::string(value);
}

inline std::string to_arg(const std::string& value) {
    return value;
}

inline std::string to_arg(const char* value) {
    return std::string(value);
}

template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<std::decay_t<T>>>>
inline std::string to_arg(T value) {
    if constexpr (std::is_floating_point_v<T>) {
        return format_double(static_cast<double>(value));
    } else {
        return std::to_string(value);
    }
}

} // namespace hotkv::detail
