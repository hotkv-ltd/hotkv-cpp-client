// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/options.hpp>

#include <chrono>
#include <cstddef>
#include <memory>

namespace hotkv::detail {

// A duplex byte stream to the server: either a plain TCP/unix socket or
// (when built with HOTKV_CPP_WITH_TLS) a TLS session over one. Internal;
// Connection is the layer that speaks RESP over this.
class IoStream {
public:
    virtual ~IoStream() = default;

    // Reads at least 1 and at most len bytes into buf; returns the count
    // read, or 0 on a clean peer shutdown. Throws hotkv::TimeoutError if
    // no byte arrives within the current receive timeout, or
    // hotkv::ConnectionError on any other I/O failure.
    virtual std::size_t read_some(char* buf, std::size_t len) = 0;

    // Writes exactly len bytes, retrying short writes internally. Throws
    // like read_some().
    virtual void write_all(const char* buf, std::size_t len) = 0;

    virtual void close() noexcept = 0;

    // Applies OS-level receive/send timeouts to the underlying socket;
    // read_some()/write_all() throw hotkv::TimeoutError once they elapse.
    virtual void set_timeouts(std::chrono::milliseconds recv, std::chrono::milliseconds send) = 0;
};

// Opens a new connection per opts (TCP host:port, or a unix domain
// socket when opts.unix_socket_path is set), applying
// opts.connect_timeout, and wraps it in TLS when opts.tls.enabled.
// Throws hotkv::ConnectionError / hotkv::TimeoutError on failure.
std::unique_ptr<IoStream> open_connection(const Options& opts);

} // namespace hotkv::detail
