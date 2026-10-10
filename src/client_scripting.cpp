// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

#include <hotkv/error.hpp>

#include "sha1.hpp"

namespace hotkv {

namespace {

std::vector<std::string> build_eval_args(const char* cmd_name, std::string_view script_or_sha,
                                          const std::vector<std::string>& keys,
                                          const std::vector<std::string>& args) {
    std::vector<std::string> out{cmd_name, std::string(script_or_sha), std::to_string(keys.size())};
    out.insert(out.end(), keys.begin(), keys.end());
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

} // namespace

Reply Client::eval(std::string_view script, const std::vector<std::string>& keys,
                    const std::vector<std::string>& args) {
    return command(build_eval_args("EVAL", script, keys, args));
}

Reply Client::evalsha(std::string_view sha1_hex, const std::vector<std::string>& keys,
                       const std::vector<std::string>& args) {
    return command(build_eval_args("EVALSHA", sha1_hex, keys, args));
}

Reply Client::eval_ro(std::string_view script, const std::vector<std::string>& keys,
                       const std::vector<std::string>& args) {
    return command(build_eval_args("EVAL_RO", script, keys, args));
}

Reply Client::evalsha_ro(std::string_view sha1_hex, const std::vector<std::string>& keys,
                          const std::vector<std::string>& args) {
    return command(build_eval_args("EVALSHA_RO", sha1_hex, keys, args));
}

Reply Client::eval_script(std::string_view script, const std::vector<std::string>& keys,
                           const std::vector<std::string>& args) {
    std::string sha1 = detail::sha1_hex(script);
    try {
        return evalsha(sha1, keys, args);
    } catch (const ServerError& e) {
        if (e.code() != "NOSCRIPT") {
            throw;
        }
        return eval(script, keys, args);
    }
}

std::string Client::script_load(std::string_view script) {
    return command({"SCRIPT", "LOAD", std::string(script)}).as_string();
}

std::vector<bool> Client::script_exists(const std::vector<std::string>& sha1_hexes) {
    std::vector<std::string> args{"SCRIPT", "EXISTS"};
    args.insert(args.end(), sha1_hexes.begin(), sha1_hexes.end());
    Reply r = command(std::move(args));
    std::vector<bool> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_int() != 0);
    }
    return out;
}

void Client::script_flush() {
    command({"SCRIPT", "FLUSH"});
}

} // namespace hotkv
