// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/options.hpp>

#include <hotkv/error.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace hotkv {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int hi = hex_digit(in[i + 1]);
            int lo = hex_digit(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

bool parse_bool(const std::string& v, bool fallback) {
    std::string lv = to_lower(v);
    if (lv == "true" || lv == "1" || lv == "yes" || lv == "on") return true;
    if (lv == "false" || lv == "0" || lv == "no" || lv == "off") return false;
    return fallback;
}

long parse_long(const std::string& v, const char* what);

// Parses an integer URL component and checks it lies in [lo, hi].
long parse_ranged(const std::string& v, const char* what, long lo, long hi) {
    long value = parse_long(v, what);
    if (value < lo || value > hi) {
        throw Error(std::string("hotkv: ") + what + " in URL out of range (" + std::to_string(lo) + " to " +
                    std::to_string(hi) + "): " + v);
    }
    return value;
}

long parse_long(const std::string& v, const char* what) {
    if (v.empty()) {
        throw Error(std::string("hotkv: invalid ") + what + " in URL: (empty)");
    }
    char* end = nullptr;
    long value = std::strtol(v.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        throw Error(std::string("hotkv: invalid ") + what + " in URL: \"" + v + "\"");
    }
    return value;
}

std::vector<std::pair<std::string, std::string>> parse_query(const std::string& query) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t start = 0;
    while (start <= query.size()) {
        std::size_t amp = query.find('&', start);
        std::string pair = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!pair.empty()) {
            std::size_t eq = pair.find('=');
            std::string key = percent_decode(eq == std::string::npos ? pair : pair.substr(0, eq));
            std::string value = eq == std::string::npos ? std::string() : percent_decode(pair.substr(eq + 1));
            out.emplace_back(std::move(key), std::move(value));
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
    return out;
}

} // namespace

Options Options::parse_url(const std::string& url) {
    std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        // The URL is not echoed: it may carry a password.
        throw Error("hotkv: invalid URL (missing \"scheme://\")");
    }
    std::string scheme = to_lower(url.substr(0, scheme_end));

    bool use_tls = false;
    if (scheme == "hotkv" || scheme == "redis") {
        use_tls = false;
    } else if (scheme == "hotkvs" || scheme == "rediss") {
        use_tls = true;
    } else {
        throw Error("hotkv: unsupported URL scheme \"" + scheme + "\" (want hotkv, hotkvs, redis, or rediss)");
    }

    std::string rest = url.substr(scheme_end + 3);

    std::string query;
    std::size_t qpos = rest.find('?');
    if (qpos != std::string::npos) {
        query = rest.substr(qpos + 1);
        rest = rest.substr(0, qpos);
    }

    std::string path;
    std::size_t slash = rest.find('/');
    if (slash != std::string::npos) {
        path = rest.substr(slash + 1);
        rest = rest.substr(0, slash);
    }

    std::string authority = rest;
    std::string userinfo;
    std::size_t at = authority.rfind('@');
    if (at != std::string::npos) {
        userinfo = authority.substr(0, at);
        authority = authority.substr(at + 1);
    }

    Options opts;

    if (!userinfo.empty()) {
        std::size_t colon = userinfo.find(':');
        if (colon != std::string::npos) {
            opts.username = percent_decode(userinfo.substr(0, colon));
            opts.password = percent_decode(userinfo.substr(colon + 1));
        } else {
            // A single userinfo component with no colon is the legacy
            // single-password AUTH form (e.g. redis://:secret@host is the
            // canonical spelling, but redis://secret@host is accepted too).
            opts.password = percent_decode(userinfo);
        }
    }

    // host[:port], with minimal IPv6 literal support ("[::1]:6399").
    std::string host_part = authority;
    std::string port_part;
    if (!host_part.empty() && host_part.front() == '[') {
        std::size_t close = host_part.find(']');
        if (close == std::string::npos) {
            throw Error("hotkv: invalid IPv6 host in URL: \"" + authority + "\"");
        }
        opts.host = host_part.substr(1, close - 1);
        if (close + 1 < host_part.size() && host_part[close + 1] == ':') {
            port_part = host_part.substr(close + 2);
        }
    } else {
        std::size_t colon = host_part.find(':');
        if (colon != std::string::npos) {
            opts.host = host_part.substr(0, colon);
            port_part = host_part.substr(colon + 1);
        } else {
            opts.host = host_part;
        }
    }
    if (opts.host.empty()) {
        opts.host = "127.0.0.1";
    }
    opts.port = port_part.empty() ? static_cast<uint16_t>(6379) : static_cast<uint16_t>(parse_ranged(port_part, "port", 1, 65535));

    if (!path.empty()) {
        opts.database = static_cast<int>(parse_ranged(path, "database index", 0, 1000000));
    }

    opts.tls.enabled = use_tls;
    if (use_tls) {
        opts.tls.server_name = opts.host;
    }

    for (const auto& [key, value] : parse_query(query)) {
        std::string lk = to_lower(key);
        if (lk == "client_name") {
            opts.client_name = value;
        } else if (lk == "connect_timeout_ms") {
            opts.connect_timeout = std::chrono::milliseconds(parse_ranged(value, "connect_timeout_ms", 0, 86400000));
        } else if (lk == "command_timeout_ms") {
            opts.command_timeout = std::chrono::milliseconds(parse_ranged(value, "command_timeout_ms", 0, 86400000));
        } else if (lk == "pool_size") {
            opts.pool_size = static_cast<std::size_t>(parse_ranged(value, "pool_size", 1, 100000));
        } else if (lk == "pool_idle_timeout_ms") {
            opts.pool_idle_timeout = std::chrono::milliseconds(parse_ranged(value, "pool_idle_timeout_ms", 0, 86400000));
        } else if (lk == "pool_health_check_after_ms") {
            opts.pool_health_check_after = std::chrono::milliseconds(parse_ranged(value, "pool_health_check_after_ms", 0, 86400000));
        } else if (lk == "resp3") {
            opts.use_resp3 = parse_bool(value, opts.use_resp3);
        } else if (lk == "unix_socket") {
            opts.unix_socket_path = value;
        } else if (lk == "tls_ca_file") {
            opts.tls.ca_file = value;
            opts.tls.enabled = true;
        } else if (lk == "tls_cert_file") {
            opts.tls.cert_file = value;
            opts.tls.enabled = true;
        } else if (lk == "tls_key_file") {
            opts.tls.key_file = value;
            opts.tls.enabled = true;
        } else if (lk == "tls_server_name") {
            opts.tls.server_name = value;
        } else if (lk == "tls_insecure_skip_verify") {
            opts.tls.insecure_skip_verify = parse_bool(value, false);
        }
        // Unknown query parameters are ignored rather than rejected, so
        // future options can be added without breaking existing URLs.
    }

    return opts;
}

} // namespace hotkv
