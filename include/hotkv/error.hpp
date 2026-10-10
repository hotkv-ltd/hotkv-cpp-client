// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <stdexcept>
#include <string>

namespace hotkv {

// Base class for every exception the SDK throws. Catch this to handle any
// HotKV client failure generically, or one of the subclasses below to
// handle a specific failure mode.
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

// Failed to establish, or lost, a connection to the server (DNS failure,
// refused connection, reset, unexpected EOF, TLS handshake failure, pool
// exhaustion). Reads tagged idempotent are retried once transparently
// before this is thrown; see Options for retry behavior.
class ConnectionError : public Error {
public:
    explicit ConnectionError(const std::string& message) : Error(message) {}
};

// A connect, command or pool-acquire operation exceeded its configured
// timeout. Always also a ConnectionError.
class TimeoutError : public ConnectionError {
public:
    explicit TimeoutError(const std::string& message) : ConnectionError(message) {}
};

// The server sent bytes that do not form a valid RESP2/RESP3 reply, or an
// otherwise well-formed reply appeared where the protocol state machine
// did not expect one (for example inside a MULTI/EXEC transcript).
class ProtocolError : public Error {
public:
    explicit ProtocolError(const std::string& message) : Error(message) {}
};

// A Reply's typed accessor (as_int(), as_array(), ...) was called on a
// reply of an incompatible RESP type. Distinct from ProtocolError, which
// means the bytes on the wire were malformed; this means the bytes parsed
// fine but the caller asked for the wrong shape.
class ReplyTypeError : public Error {
public:
    explicit ReplyTypeError(const std::string& message) : Error(message) {}
};

// A caller-supplied argument was rejected locally, before anything was
// sent, because no HotKV server would accept it (a NaN, a negative USD
// amount, a USD amount too large for micro-dollar precision, ...).
class InvalidArgumentError : public Error {
public:
    explicit InvalidArgumentError(const std::string& message) : Error(message) {}
};

// The server replied with a RESP error (simple `-...` or RESP3 blob
// `!...`). code() is the leading token of the error message, upper-cased
// by convention on the wire (e.g. "WRONGTYPE", "NOAUTH", "ERR"); it is the
// empty string only if the server sent an error with no leading token.
class ServerError : public Error {
public:
    ServerError(std::string code, const std::string& message) : Error(message), code_(std::move(code)) {}

    const std::string& code() const noexcept { return code_; }

private:
    std::string code_;
};

// The server rejected the command with `-NOLICENSE ...`: the requested
// HotKV enterprise feature is not enabled by the server's current
// license. Integration tests should catch this and skip, since it means
// the test server's license does not cover that feature rather than that
// the SDK or server is broken.
class NotLicensedError : public ServerError {
public:
    explicit NotLicensedError(const std::string& message) : ServerError("NOLICENSE", message) {}
};

// Base class for cluster slot redirects (`-MOVED` / `-ASK`). slot() is
// the hash slot the server says owns the key; host()/port() name the node
// to redirect to. ClusterClient handles these internally; a bare Client
// talking to a cluster node surfaces them so the caller can redirect
// itself if it is not using ClusterClient.
class ClusterRedirectError : public ServerError {
public:
    ClusterRedirectError(std::string code, const std::string& message, int slot, std::string host, int port)
        : ServerError(std::move(code), message), slot_(slot), host_(std::move(host)), port_(port) {}

    int slot() const noexcept { return slot_; }
    const std::string& host() const noexcept { return host_; }
    int port() const noexcept { return port_; }

private:
    int slot_;
    std::string host_;
    int port_;
};

class MovedError : public ClusterRedirectError {
public:
    MovedError(const std::string& message, int slot, std::string host, int port)
        : ClusterRedirectError("MOVED", message, slot, std::move(host), port) {}
};

class AskError : public ClusterRedirectError {
public:
    AskError(const std::string& message, int slot, std::string host, int port)
        : ClusterRedirectError("ASK", message, slot, std::move(host), port) {}
};

// Parses a raw RESP error message (e.g. "WRONGTYPE Operation against a
// key...", "MOVED 3999 127.0.0.1:6400") and throws the most specific
// matching exception type above. Never returns.
[[noreturn]] void throw_server_error(const std::string& message);

} // namespace hotkv
