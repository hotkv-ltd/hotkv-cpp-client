// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/error.hpp>

#include <cstdlib>

namespace hotkv {

namespace {

std::string extract_code(const std::string& message) {
    auto pos = message.find(' ');
    return pos == std::string::npos ? message : message.substr(0, pos);
}

long parse_long_or(const std::string& text, long fallback) {
    if (text.empty()) {
        return fallback;
    }
    char* end = nullptr;
    long value = std::strtol(text.c_str(), &end, 10);
    return (end != nullptr && *end == '\0') ? value : fallback;
}

} // namespace

[[noreturn]] void throw_server_error(const std::string& message) {
    const std::string code = extract_code(message);

    if (code == "NOLICENSE") {
        throw NotLicensedError(message);
    }

    if (code == "MOVED" || code == "ASK") {
        // Wire form: "MOVED <slot> <host>:<port>" / "ASK <slot> <host>:<port>".
        std::size_t first_space = message.find(' ');
        std::size_t second_space =
            first_space == std::string::npos ? std::string::npos : message.find(' ', first_space + 1);
        std::string slot_str;
        std::string hostport;
        if (first_space != std::string::npos && second_space != std::string::npos) {
            slot_str = message.substr(first_space + 1, second_space - first_space - 1);
            hostport = message.substr(second_space + 1);
        }
        int slot = static_cast<int>(parse_long_or(slot_str, -1));
        std::string host;
        int port = 0;
        std::size_t colon = hostport.find_last_of(':');
        if (colon != std::string::npos) {
            host = hostport.substr(0, colon);
            port = static_cast<int>(parse_long_or(hostport.substr(colon + 1), 0));
        } else {
            host = hostport;
        }
        if (code == "MOVED") {
            throw MovedError(message, slot, std::move(host), port);
        }
        throw AskError(message, slot, std::move(host), port);
    }

    throw ServerError(code, message);
}

} // namespace hotkv
