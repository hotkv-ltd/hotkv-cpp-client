// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/options.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hotkv {

namespace detail {
class SubscriberImpl;
}

// One pub/sub message, as delivered by Subscriber. `kind` is exactly the
// frame type word the server sent ("message", "pmessage" or "smessage");
// `pattern` is set only for "pmessage".
struct Message {
    std::string kind;
    std::string channel;
    std::string pattern;
    std::string payload;
};

// A dedicated pub/sub connection: SUBSCRIBE, PSUBSCRIBE, SSUBSCRIBE and
// their UNSUBSCRIBE counterparts, with RESP3 push frames handled
// transparently on the same connection as RESP2 array frames. Construct
// via Client::create_subscriber(). Move-only, not thread-safe (except
// that listen()'s background thread and stop()/the destructor coordinate
// safely with each other).
//
// Use EITHER next_message() in your own loop, OR listen() with a
// callback; mixing the two on one Subscriber is not supported.
class Subscriber {
public:
    Subscriber(Subscriber&&) noexcept;
    Subscriber& operator=(Subscriber&&) noexcept;
    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    ~Subscriber();

    void subscribe(const std::vector<std::string>& channels);
    void psubscribe(const std::vector<std::string>& patterns);
    void ssubscribe(const std::vector<std::string>& channels);
    // An empty list unsubscribes from everything of that kind.
    void unsubscribe(const std::vector<std::string>& channels = {});
    void punsubscribe(const std::vector<std::string>& patterns = {});
    void sunsubscribe(const std::vector<std::string>& channels = {});

    // Blocks until a message arrives, skipping over subscribe/
    // unsubscribe confirmation frames, or until `timeout` elapses
    // (returning std::nullopt) if given; blocks indefinitely if not.
    std::optional<Message> next_message(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Starts a background thread that calls handler for every message
    // until stop() is called or the Subscriber is destroyed. `handler`
    // should not throw; exceptions escaping it are caught and dropped so
    // the listener thread keeps running.
    void listen(std::function<void(const Message&)> handler);
    // Stops the background thread started by listen(), if any, and
    // blocks until it has exited. Safe to call even if listen() was
    // never called.
    void stop();

private:
    friend class Client;
    explicit Subscriber(Options options);

    std::unique_ptr<detail::SubscriberImpl> impl_;
};

} // namespace hotkv
