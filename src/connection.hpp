// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include "resp_codec.hpp"
#include "socket_io.hpp"

#include <hotkv/options.hpp>
#include <hotkv/reply.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hotkv::detail {

// One physical connection to the server: a socket (or TLS session) plus
// RESP2/RESP3 framing and the connect-time handshake (HELLO with
// automatic RESP2 fallback, AUTH, CLIENT SETNAME, SELECT). Not copyable
// or movable; owned by the pool through a unique_ptr.
class Connection {
public:
    explicit Connection(const Options& opts);

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Sends one command and returns its reply (does not throw on a
    // server error reply; the caller decides whether to raise_if_error()).
    // `timeout_override`, when given, replaces the connection's normal
    // command timeout for this call only (used for blocking commands
    // like XREAD ... BLOCK, whose wait can legitimately exceed it).
    Reply send_command(const std::vector<std::string>& args,
                        std::optional<std::chrono::milliseconds> timeout_override = std::nullopt);

    // Split send/receive, for pipelines: write several commands with
    // write_command(), then read the same number of replies with
    // read_one_reply(), in order.
    void write_command(const std::vector<std::string>& args);
    Reply read_one_reply();

    // Best-effort liveness check for pool reuse; never throws.
    bool ping() noexcept;

    void close() noexcept;

    // Overrides the socket's receive/send timeouts directly; used by
    // Subscriber, which waits on its dedicated connection for arbitrary
    // (possibly unbounded) periods between messages. 0 means "block
    // indefinitely", matching the underlying setsockopt(SO_RCVTIMEO)
    // convention.
    void set_io_timeouts(std::chrono::milliseconds recv, std::chrono::milliseconds send) {
        stream_->set_timeouts(recv, send);
    }

    bool is_resp3() const noexcept { return resp3_; }
    std::chrono::steady_clock::time_point last_used() const noexcept { return last_used_; }
    void touch() noexcept { last_used_ = std::chrono::steady_clock::now(); }

private:
    void handshake(const Options& opts);
    void legacy_auth_and_setname(const Options& opts);

    std::unique_ptr<IoStream> stream_;
    RespReader reader_;
    bool resp3_ = false;
    std::chrono::milliseconds command_timeout_;
    std::chrono::steady_clock::time_point last_used_;
};

} // namespace hotkv::detail
