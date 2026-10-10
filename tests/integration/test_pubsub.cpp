// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using hotkv::Message;
using hotkv::Subscriber;
using hotkv::testutil::IntegrationTest;

namespace {
// PUBLISH can race a fresh SUBSCRIBE that has not finished registering
// server-side yet; give it a moment before publishing, and poll
// next_message() with a generous per-call timeout rather than a single
// try.
constexpr auto kMessageWait = std::chrono::milliseconds(2000);
} // namespace

TEST_F(IntegrationTest, SubscribePublishNextMessage) {
    std::string channel = key("channel");
    Subscriber sub = client_.create_subscriber();
    sub.subscribe({channel});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    client_.command({"PUBLISH", channel, "hello"});

    auto msg = sub.next_message(kMessageWait);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->kind, "message");
    EXPECT_EQ(msg->channel, channel);
    EXPECT_EQ(msg->payload, "hello");
}

TEST_F(IntegrationTest, PsubscribePublishDeliversPatternAndChannel) {
    std::string pattern = prefix_ + ":pat.*";
    std::string channel = prefix_ + ":pat.one";
    Subscriber sub = client_.create_subscriber();
    sub.psubscribe({pattern});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    client_.command({"PUBLISH", channel, "payload"});

    auto msg = sub.next_message(kMessageWait);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->kind, "pmessage");
    EXPECT_EQ(msg->pattern, pattern);
    EXPECT_EQ(msg->channel, channel);
    EXPECT_EQ(msg->payload, "payload");
}

TEST_F(IntegrationTest, SsubscribeSpublishDeliversSmessage) {
    std::string channel = key("shard");
    Subscriber sub = client_.create_subscriber();
    sub.ssubscribe({channel});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    client_.command({"SPUBLISH", channel, "shardpayload"});

    auto msg = sub.next_message(kMessageWait);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->kind, "smessage");
    EXPECT_EQ(msg->channel, channel);
    EXPECT_EQ(msg->payload, "shardpayload");
}

TEST_F(IntegrationTest, UnsubscribeStopsDelivery) {
    std::string channel = key("unsub");
    Subscriber sub = client_.create_subscriber();
    sub.subscribe({channel});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sub.unsubscribe({channel});

    // A publish after unsubscribing should not be observed; a short
    // bounded wait for a message must time out.
    client_.command({"PUBLISH", channel, "should-not-arrive"});
    auto msg = sub.next_message(std::chrono::milliseconds(300));
    EXPECT_FALSE(msg.has_value());
}

TEST_F(IntegrationTest, ListenCallbackReceivesMessage) {
    std::string channel = key("listen");
    Subscriber sub = client_.create_subscriber();
    sub.subscribe({channel});

    std::mutex mtx;
    std::condition_variable cv;
    std::optional<Message> received;

    sub.listen([&](const Message& m) {
        std::lock_guard<std::mutex> lock(mtx);
        received = m;
        cv.notify_one();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client_.command({"PUBLISH", channel, "via-callback"});

    std::unique_lock<std::mutex> lock(mtx);
    bool got = cv.wait_for(lock, kMessageWait, [&] { return received.has_value(); });
    ASSERT_TRUE(got);
    EXPECT_EQ(received->payload, "via-callback");

    sub.stop();
}

TEST_F(IntegrationTest, NextMessageTimesOutWithNoTraffic) {
    std::string channel = key("silent");
    Subscriber sub = client_.create_subscriber();
    sub.subscribe({channel});

    auto msg = sub.next_message(std::chrono::milliseconds(200));
    EXPECT_FALSE(msg.has_value());
}
