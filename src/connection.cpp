// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "connection.hpp"

#include <hotkv/error.hpp>

namespace hotkv::detail {

Connection::Connection(const Options& opts)
    : stream_(open_connection(opts)), reader_(*stream_), command_timeout_(opts.command_timeout) {
    handshake(opts);
}

void Connection::legacy_auth_and_setname(const Options& opts) {
    if (!opts.password.empty()) {
        std::vector<std::string> auth_args{"AUTH"};
        if (!opts.username.empty()) {
            auth_args.push_back(opts.username);
        }
        auth_args.push_back(opts.password);
        write_command(auth_args);
        read_one_reply().raise_if_error();
    }
    if (!opts.client_name.empty()) {
        write_command({"CLIENT", "SETNAME", opts.client_name});
        read_one_reply().raise_if_error();
    }
}

void Connection::handshake(const Options& opts) {
    stream_->set_timeouts(opts.connect_timeout, opts.connect_timeout);
    resp3_ = false;

    if (opts.use_resp3) {
        std::vector<std::string> hello_args{"HELLO", "3"};
        if (!opts.username.empty() || !opts.password.empty()) {
            hello_args.emplace_back("AUTH");
            hello_args.push_back(opts.username.empty() ? "default" : opts.username);
            hello_args.push_back(opts.password);
        }
        if (!opts.client_name.empty()) {
            hello_args.emplace_back("SETNAME");
            hello_args.push_back(opts.client_name);
        }
        write_command(hello_args);
        Reply hello_reply = read_one_reply();
        if (!hello_reply.is_error()) {
            resp3_ = true;
        } else {
            // Old server, or HELLO otherwise rejected: fall back to RESP2
            // and authenticate the classic way.
            legacy_auth_and_setname(opts);
        }
    } else {
        legacy_auth_and_setname(opts);
    }

    if (opts.database != 0) {
        write_command({"SELECT", std::to_string(opts.database)});
        read_one_reply().raise_if_error();
    }

    stream_->set_timeouts(opts.command_timeout, opts.command_timeout);
    touch();
}

void Connection::write_command(const std::vector<std::string>& args) {
    std::string encoded = encode_command(args);
    stream_->write_all(encoded.data(), encoded.size());
}

Reply Connection::read_one_reply() {
    return reader_.read_reply();
}

Reply Connection::send_command(const std::vector<std::string>& args,
                                std::optional<std::chrono::milliseconds> timeout_override) {
    std::chrono::milliseconds timeout = timeout_override.value_or(command_timeout_);
    stream_->set_timeouts(timeout, timeout);
    write_command(args);
    Reply reply = read_one_reply();
    touch();
    return reply;
}

bool Connection::ping() noexcept {
    try {
        stream_->set_timeouts(command_timeout_, command_timeout_);
        write_command({"PING"});
        Reply reply = read_one_reply();
        touch();
        return !reply.is_error();
    } catch (...) {
        return false;
    }
}

void Connection::close() noexcept {
    stream_->close();
}

} // namespace hotkv::detail
