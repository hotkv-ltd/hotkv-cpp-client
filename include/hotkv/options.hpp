// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hotkv {

// TLS connection settings, used only when TlsOptions::enabled is true.
// The fields exist regardless of how the SDK was built so callers' code
// always compiles; enabling TLS in a build without HOTKV_CPP_WITH_TLS
// throws hotkv::ConnectionError at connect time.
struct TlsOptions {
    bool enabled = false;
    // PEM CA bundle used to verify the server certificate. Empty uses the
    // platform's default trust store.
    std::string ca_file;
    // PEM client certificate / private key for mutual TLS. Both empty
    // means no client certificate is presented.
    std::string cert_file;
    std::string key_file;
    // SNI hostname and the name checked against the server certificate.
    // Empty defaults to Options::host.
    std::string server_name;
    // Disables certificate verification entirely. For local testing
    // against self-signed certificates only; never enable this against a
    // production endpoint.
    bool insecure_skip_verify = false;
};

// Every connection and client tunable. Client::connect(url) fills this in
// from a hotkv://, hotkvs://, redis:// or rediss:// URL; construct it
// directly for full control, or start from Options{} and override fields.
struct Options {
    std::string host = "127.0.0.1";
    uint16_t port = 6379;
    // When non-empty, used instead of host/port (POSIX platforms only).
    std::string unix_socket_path;

    std::string username;
    std::string password;
    int database = 0;
    std::string client_name;

    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds command_timeout{5000};

    TlsOptions tls;

    // Connection pool: at most this many physical connections are ever
    // open at once; callers block (up to connect_timeout) when the pool
    // is exhausted and all connections are in use.
    std::size_t pool_size = 8;
    // A pooled connection idle longer than this is closed instead of
    // reused. 0 disables idle eviction.
    std::chrono::milliseconds pool_idle_timeout{60000};
    // A pooled connection idle at least this long is PINGed before reuse
    // to detect a peer that closed it quietly. 0 disables the check.
    std::chrono::milliseconds pool_health_check_after{30000};

    // Negotiate RESP3 with `HELLO 3` on every new connection, falling
    // back to RESP2 automatically if the server rejects HELLO. Disable to
    // speak RESP2 only.
    bool use_resp3 = true;

    // Parses a hotkv://, hotkvs://, redis:// or rediss:// URL of the form
    // "scheme://[username[:password]@]host[:port][/database][?param=value&...]".
    // hotkvs:// and rediss:// enable TLS. Recognized query parameters:
    // client_name, connect_timeout_ms, command_timeout_ms, pool_size,
    // pool_idle_timeout_ms, pool_health_check_after_ms, resp3 (true/false),
    // unix_socket, tls_ca_file, tls_cert_file, tls_key_file,
    // tls_server_name, tls_insecure_skip_verify (true/false). Throws
    // hotkv::Error on a malformed URL.
    static Options parse_url(const std::string& url);
};

} // namespace hotkv
