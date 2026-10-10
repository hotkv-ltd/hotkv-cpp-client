// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include "connection_pool.hpp"

#include <hotkv/options.hpp>
#include <hotkv/reply.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace hotkv::detail {

// Shared state behind every hotkv::Client handle: the connection pool
// and the options it was built from. Copying a Client copies a
// shared_ptr<ClientImpl>, so every copy shares one pool.
class ClientImpl {
public:
    explicit ClientImpl(Options opts) : pool_(std::move(opts)) {}

    const Options& options() const noexcept { return pool_.options(); }

    // Executes one command end to end: borrows a connection, sends it,
    // and either returns the reply (throwing the matching ServerError
    // subclass if it is an error reply) or throws a connection-level
    // exception. A recognized idempotent read is retried once, on a
    // fresh connection, if the first attempt failed with a
    // ConnectionError. `timeout_override`, when given, replaces the
    // pool's normal command timeout for this call only (blocking
    // commands such as XREAD ... BLOCK).
    Reply command(const std::vector<std::string>& args,
                   std::optional<std::chrono::milliseconds> timeout_override = std::nullopt);

    // Used by Pipeline/Transaction/Subscriber, which need to hold one
    // connection across several round trips themselves.
    ConnectionPool& pool() noexcept { return pool_; }

private:
    ConnectionPool pool_;
};

// True for commands that are safe to retry automatically after a
// connection loss (read-only, never mutates server state). Keyed on the
// upper-cased command name, and for commands whose name alone is
// ambiguous (CONFIG, CLIENT, XINFO, ...) the upper-cased "NAME SUB" pair.
bool is_idempotent_read(const std::vector<std::string>& args);

} // namespace hotkv::detail
