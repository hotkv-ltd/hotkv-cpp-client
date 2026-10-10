// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/reply.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hotkv {

namespace detail {
class ClientImpl;
class TransactionImpl;
}

// A MULTI/EXEC transaction on one dedicated connection, with WATCH/
// UNWATCH support. Construct via Client::transaction(). Move-only: it
// owns one physical connection for its lifetime (returned to the pool,
// or dropped if left mid-transaction, on destruction). Not thread-safe.
//
//   auto tx = client.transaction();
//   tx.watch({"balance"});
//   auto before = client.get("balance"); // read the watched key via the pool, before MULTI
//   tx.multi();
//   tx.command({"DECRBY", "balance", "10"});
//   if (auto results = tx.exec()) { ... } else { /* aborted: balance changed */ }
class Transaction {
public:
    Transaction(Transaction&&) noexcept;
    Transaction& operator=(Transaction&&) noexcept;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    // Must be called before multi(), matching WATCH's own requirement.
    Transaction& watch(const std::vector<std::string>& keys);
    void unwatch();

    // Sends MULTI. Call once, after any watch() calls and before
    // command().
    Transaction& multi();

    // Queues one command inside the transaction; throws
    // hotkv::ProtocolError if the server does not reply +QUEUED.
    Transaction& command(std::vector<std::string> args);

    // Sends EXEC. Returns std::nullopt if the transaction aborted
    // (a watched key changed since watch()); otherwise the queued
    // commands' replies, in order (an individual reply may itself be an
    // error, exactly as a raw MULTI/EXEC allows).
    std::optional<std::vector<Reply>> exec();

    // Sends DISCARD, abandoning any queued commands.
    void discard();

private:
    friend class Client;
    explicit Transaction(std::shared_ptr<detail::ClientImpl> impl);

    std::unique_ptr<detail::TransactionImpl> impl_;
};

} // namespace hotkv
