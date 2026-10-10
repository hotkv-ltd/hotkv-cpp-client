// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

using hotkv::Pipeline;
using hotkv::Reply;
using hotkv::Transaction;
using hotkv::testutil::IntegrationTest;

TEST_F(IntegrationTest, PipelineRunsInOrderInOneRoundTrip) {
    std::string a = key("pa");
    std::string b = key("pb");

    Pipeline p = client_.pipeline();
    p.command({"SET", a, "1"});
    p.command({"INCR", a});
    p.command({"SET", b, "x"});
    p.command({"GET", b});
    std::vector<Reply> results = p.exec();

    ASSERT_EQ(results.size(), 4u);
    EXPECT_EQ(results[0].as_string(), "OK");
    EXPECT_EQ(results[1].as_int(), 2);
    EXPECT_EQ(results[2].as_string(), "OK");
    EXPECT_EQ(results[3].as_string(), "x");
}

TEST_F(IntegrationTest, PipelineSurfacesPerCommandErrorsWithoutFailingBatch) {
    std::string k = key("pipeerr");
    client_.set(k, "not-a-list");

    Pipeline p = client_.pipeline();
    p.command({"SET", k, "still-a-string"});
    p.command({"LPUSH", k, "x"}); // WRONGTYPE
    p.command({"GET", k});
    std::vector<Reply> results = p.exec();

    ASSERT_EQ(results.size(), 3u);
    EXPECT_FALSE(results[0].is_error());
    EXPECT_TRUE(results[1].is_error());
    EXPECT_EQ(results[1].error_code(), "WRONGTYPE");
    EXPECT_FALSE(results[2].is_error());
    EXPECT_EQ(results[2].as_string(), "still-a-string");
}

TEST_F(IntegrationTest, PipelineQueuedAndClear) {
    Pipeline p = client_.pipeline();
    p.command({"PING"});
    p.command({"PING"});
    EXPECT_EQ(p.queued(), 2u);
    p.clear();
    EXPECT_EQ(p.queued(), 0u);
}

TEST_F(IntegrationTest, TransactionCommitsAllQueuedCommands) {
    std::string k = key("txcommit");

    Transaction tx = client_.transaction();
    tx.multi();
    tx.command({"SET", k, "1"});
    tx.command({"INCR", k});
    auto results = tx.exec();

    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 2u);
    EXPECT_EQ((*results)[0].as_string(), "OK");
    EXPECT_EQ((*results)[1].as_int(), 2);
    EXPECT_EQ(*client_.get(k), "2");
}

TEST_F(IntegrationTest, TransactionDiscardAbandonsQueuedCommands) {
    std::string k = key("txdiscard");
    client_.set(k, "untouched");

    Transaction tx = client_.transaction();
    tx.multi();
    tx.command({"SET", k, "should-not-apply"});
    tx.discard();

    EXPECT_EQ(*client_.get(k), "untouched");
}

TEST_F(IntegrationTest, TransactionAbortsWhenWatchedKeyChanges) {
    std::string k = key("txwatch");
    client_.set(k, "initial");

    Transaction tx = client_.transaction();
    tx.watch({k});

    // A different connection modifies the watched key before EXEC.
    hotkv::Client interloper = hotkv::testutil::make_test_client();
    interloper.set(k, "changed-by-someone-else");

    tx.multi();
    tx.command({"SET", k, "should-not-apply"});
    auto results = tx.exec();

    EXPECT_FALSE(results.has_value()); // aborted
    EXPECT_EQ(*client_.get(k), "changed-by-someone-else");
}

TEST_F(IntegrationTest, TransactionSucceedsWhenWatchedKeyUnchanged) {
    std::string k = key("txwatchok");
    client_.set(k, "v");

    Transaction tx = client_.transaction();
    tx.watch({k});
    tx.multi();
    tx.command({"APPEND", k, "2"});
    auto results = tx.exec();

    ASSERT_TRUE(results.has_value());
    EXPECT_EQ(*client_.get(k), "v2");
}

TEST_F(IntegrationTest, TransactionUnwatchClearsWatch) {
    std::string k = key("txunwatch");
    client_.set(k, "v");

    Transaction tx = client_.transaction();
    tx.watch({k});
    tx.unwatch();

    hotkv::Client interloper = hotkv::testutil::make_test_client();
    interloper.set(k, "changed");

    tx.multi();
    tx.command({"SET", k, "final"});
    auto results = tx.exec();

    ASSERT_TRUE(results.has_value()); // not aborted: watch was cleared
    EXPECT_EQ(*client_.get(k), "final");
}
