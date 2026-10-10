// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/client.hpp>
#include <hotkv/options.hpp>
#include <hotkv/reply.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hotkv {

// Raw CRC16/XMODEM checksum (polynomial 0x1021, initial value 0, no
// input/output reflection): crc16("123456789") == 0x31C3. The variant
// HotKV cluster mode uses for hash slots.
uint16_t crc16(std::string_view data);

// The cluster hash slot (0..16383) for `key`, honoring a `{hash tag}`
// substring exactly like CLUSTER KEYSLOT: if key contains '{' with a
// later '}' and at least one byte between them, only the bytes between
// the first such pair are hashed; otherwise the whole key is hashed.
uint16_t key_slot(std::string_view key);

namespace detail {
class ClusterClientImpl;
}

// A cluster-aware client. Builds a slot map from CLUSTER SLOTS (falling
// back to CLUSTER SHARDS), routes each command by its key's hash slot
// (CRC16, honoring hash tags), and follows MOVED (refreshing the
// affected slot) and ASK (sending ASKING first, on the same connection)
// redirects, up to a bounded number of hops per command, and backs off and
// retries TRYAGAIN / CLUSTERDOWN / LOADING / MASTERDOWN a bounded number of
// times. Thread-safe and cheap to copy: like Client, it is a small handle
// to shared state.
//
// Commands are routed by their first key (args[1] for most commands; the
// real key for EVAL/EVALSHA with keys, XREAD/XREADGROUP, ZUNION and the other
// numkeys commands, OBJECT/XINFO/BITOP, ...); keyless commands go to any
// node. Commands that touch several slots at once are a cluster
// limitation this SDK does not work around (use `{hash tag}`s to co-locate
// keys, as with any cluster client).
class ClusterClient {
public:
    // `seed_url`/`seed_options` name any one reachable cluster node; its
    // TLS/auth/timeout settings are reused as a template when connecting
    // to the other nodes the slot map discovers.
    static ClusterClient connect(const std::string& seed_url);
    static ClusterClient connect(const Options& seed_options);
    explicit ClusterClient(Options seed_options);

    // Re-fetches the slot map now, instead of waiting for the next MOVED
    // redirect (or the first command) to trigger it.
    void refresh_slots();

    // Sends any command, extracting the routing key from args[1] when
    // present (the common case for single-key commands); with no second
    // argument, routes to an arbitrary known node.
    Reply command(std::vector<std::string> args);

    // Sends any command, routed explicitly by `key`'s hash slot; use
    // this when the command's key is not args[1].
    Reply command_for_key(std::string_view key, std::vector<std::string> args);

    // When true, read commands (the SDK's idempotent-read list: GET,
    // HGETALL, ZRANGE, ...) are routed to a replica (sending READONLY
    // first, on the same connection) instead of the slot's primary.
    // Every other command still goes to the primary. Off by default.
    void set_read_from_replicas(bool enabled);

private:
    std::shared_ptr<detail::ClusterClientImpl> impl_;
};

} // namespace hotkv
