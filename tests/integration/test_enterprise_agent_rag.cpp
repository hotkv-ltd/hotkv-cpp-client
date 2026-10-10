// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <algorithm>

using hotkv::testutil::IntegrationTest;

// AMEM.* needs a license with the agent-memory feature; without it these
// skip cleanly.

TEST_F(IntegrationTest, AmemConvCreateAppendGetWindowClearDrop) {
    std::string session = key("amemconv");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.amem().conv().create(session, 10), "agent-memory");
    client_.amem().conv().append(session, "user", "hi there");
    client_.amem().conv().append(session, "assistant", "hello!");

    auto turns = client_.amem().conv().get(session);
    ASSERT_EQ(turns.size(), 2u);
    EXPECT_EQ(turns[0].role, "user");
    EXPECT_EQ(turns[0].content, "hi there");

    auto windowed = client_.amem().conv().window(session, 1);
    ASSERT_EQ(windowed.size(), 1u);
    EXPECT_EQ(windowed[0].role, "assistant");

    EXPECT_TRUE(client_.amem().conv().clear(session));
    EXPECT_TRUE(client_.amem().conv().drop(session));
}

TEST_F(IntegrationTest, AmemSemCreateStoreRecallForget) {
    std::string agent = key("amemsem");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.amem().sem().create(agent, 3), "agent-memory");
    client_.amem().sem().store(agent, "fact1", "the sky is blue", {1.0, 0.0, 0.0});
    client_.amem().sem().store(agent, "fact2", "grass is green", {0.0, 1.0, 0.0});

    auto recalled = client_.amem().sem().recall(agent, 1, {1.0, 0.0, 0.0});
    ASSERT_FALSE(recalled.empty());
    EXPECT_EQ(recalled[0].key, "fact1");

    auto keys = client_.amem().sem().list(agent);
    EXPECT_EQ(keys.size(), 2u);
    EXPECT_TRUE(client_.amem().sem().forget(agent, "fact2"));
}

TEST_F(IntegrationTest, AmemWorkSetGetDelClearList) {
    std::string session = key("amemwork");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.amem().work().set(session, "scratch1", "value1"), "agent-memory");
    EXPECT_EQ(client_.amem().work().get(session, "scratch1"), "value1");
    EXPECT_EQ(client_.amem().work().list(session).size(), 1u);
    EXPECT_TRUE(client_.amem().work().del(session, "scratch1"));
    client_.amem().work().set(session, "another", "v");
    EXPECT_TRUE(client_.amem().work().clear(session));
}

TEST_F(IntegrationTest, AmemEpisLogQueryClearStats) {
    std::string agent = key("amemepis");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.amem().epis().log(agent, "tool_call", "called search()"), "agent-memory");
    client_.amem().epis().log(agent, "tool_result", "found 3 results");

    hotkv::AmemEpisQueryOptions opts;
    opts.type = "tool_call";
    auto events = client_.amem().epis().query(agent, opts);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].content, "called search()");

    auto stats = client_.amem().epis().stats(agent);
    EXPECT_EQ(stats.total_events, 2);

    EXPECT_TRUE(client_.amem().epis().clear(agent));
}

TEST_F(IntegrationTest, AmemCrossMemorySearch) {
    std::string agent = key("amemsearch");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.amem().sem().create(agent, 2), "agent-memory");
    client_.amem().sem().store(agent, "k1", "some content", {1.0, 0.0});

    auto results = client_.amem().search(agent, 5, {1.0, 0.0});
    EXPECT_FALSE(results.empty());
}

// AMEM.SUMMARY.* needs a license with the agent-memory summary feature.
TEST_F(IntegrationTest, AmemSummaryFamilySkipsWithoutFullLicense) {
    std::string agent = key("amemsummary");
    int64_t version = 0;
    try {
        version = client_.amem().summary().set(agent, "The user is planning a trip to Japan.");
    } catch (const hotkv::NotLicensedError&) {
        GTEST_SKIP() << "server license does not include 'amem-summary'";
    }
    EXPECT_GE(version, 1);
    client_.amem().summary().append(agent, " They prefer trains over flights.");
    auto got = client_.amem().summary().get(agent);
    ASSERT_TRUE(got.has_value());
    EXPECT_FALSE(got->summary.empty());
    client_.amem().summary().list();
    EXPECT_TRUE(client_.amem().summary().drop(agent));
}

// RAG.* needs a license with the RAG pipeline feature; without it this
// skips cleanly.
TEST_F(IntegrationTest, RagCreateIngestSearchStatsDrop) {
    std::string pipeline = key("rag");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.rag().create(pipeline, 3), "rag-pipeline");

    int64_t chunks = client_.rag().ingest(pipeline, "doc1", "The quick brown fox jumps over the lazy dog.");
    EXPECT_GE(chunks, 1);

    hotkv::RagSearchOptions opts;
    opts.k = 3;
    auto results = client_.rag().search(pipeline, "quick fox", opts);
    EXPECT_FALSE(results.empty());

    auto stats = client_.rag().stats(pipeline);
    EXPECT_EQ(stats.name, pipeline);
    EXPECT_GE(stats.total_chunks, 1);

    auto names = client_.rag().list();
    EXPECT_NE(std::find(names.begin(), names.end(), pipeline), names.end());

    client_.rag().drop(pipeline);
}

// RATELIMIT.* needs a license with the rate-limiting feature; without it
// this skips cleanly.
TEST_F(IntegrationTest, RateLimitCreateCheckPeekResetStatsDrop) {
    std::string name = key("ratelimit");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.ratelimit().create(name, 2, 10000), "rate-limiting");

    auto first = client_.ratelimit().check(name, "user1");
    EXPECT_TRUE(first.allowed);
    client_.ratelimit().check(name, "user1");
    auto third = client_.ratelimit().check(name, "user1");
    EXPECT_FALSE(third.allowed);

    auto peeked = client_.ratelimit().peek(name, "user1");
    EXPECT_EQ(peeked.current, third.current); // PEEK must not mutate state further

    client_.ratelimit().reset(name, "user1");
    auto stats = client_.ratelimit().stats(name);
    EXPECT_EQ(stats.name, name);
    client_.ratelimit().list();
    client_.ratelimit().drop(name);
}

// TS.* needs a license with the time-series feature; without it this
// skips cleanly.
TEST_F(IntegrationTest, TimeSeriesCreateAddRangeAggInfoDelete) {
    std::string k = key("ts");
    hotkv::TsCreateOptions opts;
    opts.labels = {{"region", "eu"}};
    HOTKV_SKIP_IF_NOT_LICENSED(client_.ts().create(k, opts), "time-series");

    client_.ts().add(k, 1000, 1.0);
    client_.ts().add(k, 2000, 2.0);
    client_.ts().add(k, 3000, 3.0);

    auto range = client_.ts().range(k, "0", "+");
    EXPECT_EQ(range.size(), 3u);

    auto agg = client_.ts().agg(k, "0", "+", 10000, "avg");
    ASSERT_EQ(agg.size(), 1u);
    EXPECT_DOUBLE_EQ(agg[0].value, 2.0);

    auto info = client_.ts().info(k);
    EXPECT_EQ(info.key, k);
    EXPECT_EQ(info.total_samples, 3);
    ASSERT_FALSE(info.labels.empty());
    EXPECT_EQ(info.labels[0].first, "region");

    client_.ts().set_retention(k, 3'600'000);

    std::string dst = key("tsdownsample");
    client_.ts().create(dst);
    client_.ts().downsample(k, dst, "avg", 60000);

    auto listed = client_.ts().list("region", "eu");
    EXPECT_NE(std::find(listed.begin(), listed.end(), k), listed.end());

    client_.ts().del(k);
    client_.ts().del(dst);
}

// FEATURE.* needs a license with the feature-store feature; without it
// this skips cleanly.
TEST_F(IntegrationTest, FeatureStoreCreateSetGetBatchSchemaDrop) {
    std::string group = key("features");
    HOTKV_SKIP_IF_NOT_LICENSED(
        client_.feature().create(group, "user features", {{"age", "int64"}, {"score", "double"}}),
        "feature-store");

    client_.feature().set(group, "user1", {{"age", "30"}, {"score", "0.75"}});

    auto record = client_.feature().get(group, "user1");
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->values.size(), 2u);

    auto batch = client_.feature().batch(group, {"user1", "missing-user"});
    ASSERT_EQ(batch.size(), 2u);
    EXPECT_TRUE(batch[0].has_value());
    EXPECT_FALSE(batch[1].has_value());

    auto schema = client_.feature().schema(group);
    EXPECT_EQ(schema.size(), 2u);

    auto names = client_.feature().list();
    EXPECT_NE(std::find(names.begin(), names.end(), group), names.end());

    auto stats = client_.feature().stats(group);
    EXPECT_GE(stats.entities, 1);

    client_.feature().drop(group);
}
