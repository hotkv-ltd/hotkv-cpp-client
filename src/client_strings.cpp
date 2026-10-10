// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

namespace {

void append_set_options(std::vector<std::string>& args, const SetOptions& opts) {
    if (opts.ex.has_value()) {
        args.emplace_back("EX");
        args.push_back(std::to_string(opts.ex->count()));
    } else if (opts.px.has_value()) {
        args.emplace_back("PX");
        args.push_back(std::to_string(opts.px->count()));
    } else if (opts.exat.has_value()) {
        args.emplace_back("EXAT");
        args.push_back(std::to_string(*opts.exat));
    } else if (opts.pxat.has_value()) {
        args.emplace_back("PXAT");
        args.push_back(std::to_string(*opts.pxat));
    } else if (opts.keepttl) {
        args.emplace_back("KEEPTTL");
    }
    if (opts.nx) {
        args.emplace_back("NX");
    } else if (opts.xx) {
        args.emplace_back("XX");
    }
}

} // namespace

std::optional<std::string> Client::get(std::string_view key) {
    return command({"GET", std::string(key)}).as_optional_string();
}

bool Client::set(std::string_view key, std::string_view value, const SetOptions& opts) {
    std::vector<std::string> args{"SET", std::string(key), std::string(value)};
    append_set_options(args, opts);
    return !command(std::move(args)).is_nil();
}

std::optional<std::string> Client::set_get(std::string_view key, std::string_view value, const SetOptions& opts) {
    std::vector<std::string> args{"SET", std::string(key), std::string(value)};
    append_set_options(args, opts);
    args.emplace_back("GET");
    return command(std::move(args)).as_optional_string();
}

std::optional<std::string> Client::getset(std::string_view key, std::string_view value) {
    return command({"GETSET", std::string(key), std::string(value)}).as_optional_string();
}

std::optional<std::string> Client::getdel(std::string_view key) {
    return command({"GETDEL", std::string(key)}).as_optional_string();
}

bool Client::setnx(std::string_view key, std::string_view value) {
    return command({"SETNX", std::string(key), std::string(value)}).as_int() != 0;
}

void Client::setex(std::string_view key, int64_t seconds, std::string_view value) {
    command({"SETEX", std::string(key), std::to_string(seconds), std::string(value)});
}

void Client::psetex(std::string_view key, int64_t milliseconds, std::string_view value) {
    command({"PSETEX", std::string(key), std::to_string(milliseconds), std::string(value)});
}

int64_t Client::append(std::string_view key, std::string_view value) {
    return command({"APPEND", std::string(key), std::string(value)}).as_int();
}

int64_t Client::strlen(std::string_view key) {
    return command({"STRLEN", std::string(key)}).as_int();
}

int64_t Client::incr(std::string_view key) {
    return command({"INCR", std::string(key)}).as_int();
}

int64_t Client::incrby(std::string_view key, int64_t delta) {
    return command({"INCRBY", std::string(key), std::to_string(delta)}).as_int();
}

double Client::incrbyfloat(std::string_view key, double delta) {
    return command({"INCRBYFLOAT", std::string(key), detail::format_finite(delta)}).as_double();
}

int64_t Client::decr(std::string_view key) {
    return command({"DECR", std::string(key)}).as_int();
}

int64_t Client::decrby(std::string_view key, int64_t delta) {
    return command({"DECRBY", std::string(key), std::to_string(delta)}).as_int();
}

std::vector<std::optional<std::string>> Client::mget(const std::vector<std::string>& keys) {
    std::vector<std::string> args{"MGET"};
    args.insert(args.end(), keys.begin(), keys.end());
    Reply r = command(std::move(args));
    std::vector<std::optional<std::string>> out;
    out.reserve(r.as_array().size());
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_optional_string());
    }
    return out;
}

void Client::mset(const std::vector<std::pair<std::string, std::string>>& kvs) {
    std::vector<std::string> args{"MSET"};
    for (const auto& [k, v] : kvs) {
        args.push_back(k);
        args.push_back(v);
    }
    command(std::move(args));
}

bool Client::msetnx(const std::vector<std::pair<std::string, std::string>>& kvs) {
    std::vector<std::string> args{"MSETNX"};
    for (const auto& [k, v] : kvs) {
        args.push_back(k);
        args.push_back(v);
    }
    return command(std::move(args)).as_int() != 0;
}

std::string Client::getrange(std::string_view key, int64_t start, int64_t end) {
    return command({"GETRANGE", std::string(key), std::to_string(start), std::to_string(end)}).as_string();
}

int64_t Client::setrange(std::string_view key, int64_t offset, std::string_view value) {
    return command({"SETRANGE", std::string(key), std::to_string(offset), std::string(value)}).as_int();
}

} // namespace hotkv
