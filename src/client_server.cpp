// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

bool Client::ping() {
    return command({"PING"}).as_string() == "PONG";
}

std::string Client::ping(std::string_view message) {
    Reply r = command({"PING", std::string(message)});
    return r.as_string();
}

std::string Client::echo(std::string_view message) {
    return command({"ECHO", std::string(message)}).as_string();
}

std::string Client::info(std::string_view section) {
    std::vector<std::string> args{"INFO"};
    if (!section.empty()) {
        args.emplace_back(section);
    }
    return command(std::move(args)).as_string();
}

int64_t Client::dbsize() {
    return command({"DBSIZE"}).as_int();
}

void Client::flushdb(bool async) {
    std::vector<std::string> args{"FLUSHDB"};
    if (async) {
        args.emplace_back("ASYNC");
    }
    command(std::move(args));
}

void Client::flushall(bool async) {
    std::vector<std::string> args{"FLUSHALL"};
    if (async) {
        args.emplace_back("ASYNC");
    }
    command(std::move(args));
}

ServerTime Client::time() {
    Reply r = command({"TIME"});
    const Reply::Array& arr = r.as_array();
    ServerTime t;
    if (arr.size() >= 2) {
        t.seconds = arr[0].as_int();
        t.microseconds = arr[1].as_int();
    }
    return t;
}

std::vector<std::pair<std::string, std::string>> Client::config_get(std::string_view parameter) {
    Reply r = command({"CONFIG", "GET", std::string(parameter)});
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& kv : r.as_pairs()) {
        out.emplace_back(kv.first, kv.second.is_nil() ? std::string() : kv.second.as_string());
    }
    return out;
}

void Client::config_set(std::string_view parameter, std::string_view value) {
    command({"CONFIG", "SET", std::string(parameter), std::string(value)});
}

void Client::client_setname(std::string_view name) {
    command({"CLIENT", "SETNAME", std::string(name)});
}

std::optional<std::string> Client::client_getname() {
    Reply r = command({"CLIENT", "GETNAME"});
    if (r.is_nil()) {
        return std::nullopt;
    }
    std::string name = r.as_string();
    if (name.empty()) {
        return std::nullopt;
    }
    return name;
}

int64_t Client::client_id() {
    return command({"CLIENT", "ID"}).as_int();
}

} // namespace hotkv
