// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/reply.hpp>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hotkv {

namespace detail {
class ClientImpl;
}

// Queues commands and sends them to the server in a single round trip.
// Construct via Client::pipeline(). Not thread-safe: use one Pipeline
// per thread, and do not call exec() concurrently with command().
//
//   auto p = client.pipeline();
//   p.command({"INCR", "a"}).command({"INCR", "b"});
//   for (auto& reply : p.exec()) { ... }
class Pipeline {
public:
    Pipeline& command(std::vector<std::string> args);

    // Sends every queued command in one round trip and returns their
    // replies, in order, then clears the queue. A per-command server
    // error comes back as a Reply with is_error() true rather than being
    // thrown, so one failing command never hides the others' results;
    // call raise_if_error() on a reply yourself if you want it thrown. A
    // connection-level failure (lost connection, timeout) throws as
    // usual.
    std::vector<Reply> exec();

    std::size_t queued() const noexcept { return commands_.size(); }
    void clear() noexcept { commands_.clear(); }

private:
    friend class Client;
    explicit Pipeline(std::shared_ptr<detail::ClientImpl> impl);

    std::shared_ptr<detail::ClientImpl> impl_;
    std::vector<std::vector<std::string>> commands_;
};

} // namespace hotkv
