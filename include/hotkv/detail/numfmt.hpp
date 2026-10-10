// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

// Locale-independent number formatting and parsing for the wire.
//
// Everything here is built on std::to_chars / std::from_chars, which never
// consult the C or C++ global locale (std::to_string, snprintf, ostream
// and strtod/stod all do, so a process that has called setlocale(LC_ALL,
// "de_DE.UTF-8") would otherwise send "1,5" and fail to parse "1.5").

#include <hotkv/error.hpp>

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <locale.h>
#else
#include <locale.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace hotkv::detail {

// A caller-supplied argument the server could never accept (NaN, a
// negative amount, a value too large for the field). Thrown before
// anything is sent.
[[noreturn]] inline void throw_bad_number(const char* what, double value) {
    std::string shown;
    if (std::isnan(value)) {
        shown = "nan";
    } else if (std::isinf(value)) {
        shown = value < 0 ? "-inf" : "inf";
    } else {
        char buf[64];
        auto r = std::to_chars(buf, buf + sizeof(buf), value);
        shown.assign(buf, r.ptr);
    }
    throw InvalidArgumentError(std::string("hotkv: invalid ") + what + ": " + shown);
}

// General variant: the shortest decimal text that round-trips the double
// exactly ("0.1", "5", "-0", "1e-05", "1e+21"; exponent form appears only
// when it is shorter). Infinities are "inf"/"-inf", which the core
// commands that take scores (ZADD, ZINCRBY, ZRANGE bounds, weights)
// accept. NaN is rejected: no HotKV command accepts it.
inline std::string format_double(double value) {
    if (std::isnan(value)) {
        throw_bad_number("floating point argument (NaN)", value);
    }
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, r.ptr);
}

// For rendering a reply value back into text (never throws): like
// format_double, but NaN becomes "nan".
inline std::string format_lossless(double value) {
    return std::isnan(value) ? std::string("nan") : format_double(value);
}

// Like format_double, but for arguments the server parses as a finite
// number only (vector components, thresholds, GEO coordinates and radii,
// INCRBYFLOAT deltas, rate-limit rates): NaN and +/-inf throw.
inline std::string format_finite(double value) {
    if (!std::isfinite(value)) {
        throw_bad_number("finite floating point argument", value);
    }
    return format_double(value);
}

// Finite and non-negative (a per-hit cost): negative values throw; -0 is
// sent as "0".
inline std::string format_nonnegative(double value) {
    if (!std::isfinite(value) || value < 0.0) {
        throw_bad_number("non-negative finite argument", value);
    }
    return value == 0.0 ? std::string("0") : format_double(value);
}

// USD amounts for the parsers that only take plain fixed-point text
// (LLMBUDGET.SET cost_limit_usd, LLMSTATS.PRICE amounts) or that truncate
// to micro-dollars (LLMBUDGET.CONSUME/LLMSTATS.RECORD cost_usd). Always
// "[0-9]+(.[0-9]{1,6})?": rounded to the nearest micro-dollar, trailing
// zeros trimmed, never an exponent, never a sign, no leading or trailing
// '.'. Negative and non-finite amounts throw (the server rejects them),
// as does any amount whose micro-dollar value would not fit in 64 bits.
inline std::string format_usd(double usd) {
    if (!std::isfinite(usd) || usd < 0.0) {
        throw_bad_number("USD amount (must be finite and non-negative)", usd);
    }
    // 2^64 - 1 micro-dollars is 18446744073709.551615 USD, the largest
    // amount the server's fixed-point parser accepts.
    if (usd > 18446744073709.551615) {
        throw_bad_number("USD amount (too large for micro-dollar precision)", usd);
    }
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), usd, std::chars_format::fixed, 6);
    std::string s(buf, r.ptr);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    if (s.empty() || s == "-0") {
        s = "0";
    }
    return s;
}

// Converts text that parse_double() has already validated, with the "C"
// locale regardless of the process locale. (std::from_chars for double is
// missing from older libc++ releases, so strtod_l is used instead.)
inline double strtod_c_locale(const char* text) {
#if defined(_WIN32)
    static _locale_t c_locale = _create_locale(LC_ALL, "C");
    return _strtod_l(text, nullptr, c_locale);
#else
    static locale_t c_locale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
    return strtod_l(text, nullptr, c_locale);
#endif
}

// Parses a double from a reply, locale independently: an optional sign
// ('+' too), decimal digits with an optional fraction and exponent
// ("1.5", ".5", "5.", "1e-05"), or inf/infinity/nan in any case. The whole
// text must be consumed: no whitespace, no hex floats, no digit grouping.
// Returns false on any other input or when the value overflows a double.
inline bool parse_double(std::string_view text, double& out) {
    std::size_t i = 0;
    const std::size_t n = text.size();
    if (i < n && (text[i] == '+' || text[i] == '-')) {
        ++i;
    }
    if (i >= n) {
        return false;
    }
    auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
    auto word = [&](const char* w) {
        std::size_t len = std::char_traits<char>::length(w);
        if (n - i != len) {
            return false;
        }
        for (std::size_t k = 0; k < len; ++k) {
            if (lower(text[i + k]) != w[k]) {
                return false;
            }
        }
        return true;
    };
    // Spelled out here rather than left to strtod: some C runtimes (the old
    // MinGW msvcrt) do not parse "inf" or "nan".
    const bool negative = text.front() == '-';
    if (word("inf") || word("infinity")) {
        out = negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        return true;
    }
    if (word("nan")) {
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    {
        std::size_t digits = 0;
        while (i < n && text[i] >= '0' && text[i] <= '9') {
            ++i;
            ++digits;
        }
        if (i < n && text[i] == '.') {
            ++i;
            while (i < n && text[i] >= '0' && text[i] <= '9') {
                ++i;
                ++digits;
            }
        }
        if (digits == 0) {
            return false;
        }
        if (i < n && (text[i] == 'e' || text[i] == 'E')) {
            ++i;
            if (i < n && (text[i] == '+' || text[i] == '-')) {
                ++i;
            }
            std::size_t exp_digits = 0;
            while (i < n && text[i] >= '0' && text[i] <= '9') {
                ++i;
                ++exp_digits;
            }
            if (exp_digits == 0) {
                return false;
            }
        }
        if (i != n) {
            return false;
        }
    }
    const std::string owned(text); // strtod needs a NUL terminated string
    const double value = strtod_c_locale(owned.c_str());
    if (std::isinf(value)) {
        return false; // overflow
    }
    out = value;
    return true;
}

} // namespace hotkv::detail
