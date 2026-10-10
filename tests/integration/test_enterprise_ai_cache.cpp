// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <algorithm>

using hotkv::testutil::IntegrationTest;

// SCACHE.* needs a license with the semantic-cache feature; without it
// this skips cleanly.
TEST_F(IntegrationTest, SCacheCreateSetGetStatsFlushDropList) {
    std::string ns = key("scache");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.scache().create(ns, 3), "semantic-cache");

    hotkv::Usage usage;
    usage.model = "gpt-4";
    usage.input_tokens = 10;
    usage.output_tokens = 4;
    client_.scache().set(ns, "what is the capital of france", "Paris", {1.0, 0.0, 0.0}, std::nullopt, usage);
    auto hit = client_.scache().get(ns, {1.0, 0.0, 0.0});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->response, "Paris");
    EXPECT_GT(hit->similarity, 0.9);

    auto miss = client_.scache().get(ns, {0.0, 0.0, 1.0}, 0.99);
    EXPECT_FALSE(miss.has_value());

    auto stats = client_.scache().stats(ns);
    EXPECT_GE(stats.entries, 1);
    EXPECT_GE(stats.usage.requests, 1);
    EXPECT_GE(stats.usage.input_tokens, 10);

    client_.scache().set_threshold(ns, 0.5);

    auto names = client_.scache().list();
    EXPECT_NE(std::find(names.begin(), names.end(), ns), names.end());

    EXPECT_TRUE(client_.scache().flush(ns));
    EXPECT_TRUE(client_.scache().drop(ns));
}

// PCACHE.* needs a license with the prompt-cache feature; without it
// this skips cleanly.
TEST_F(IntegrationTest, PCacheFamilySkipsWithoutFullLicense) {
    std::string ns = key("pcache");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.pcache().create(ns), "prompt-cache");

    hotkv::Usage usage;
    usage.model = "gpt-4";
    usage.input_tokens = 12;
    usage.output_tokens = 8;
    client_.pcache().set(ns, "prompt1", "response1", std::nullopt, {}, usage);
    EXPECT_EQ(client_.pcache().get(ns, "prompt1"), "response1");
    auto getex = client_.pcache().getex(ns, "prompt1");
    ASSERT_TRUE(getex.has_value());
    EXPECT_EQ(getex->response, "response1");
    auto stats = client_.pcache().stats(ns);
    EXPECT_GE(stats.entries, 1);
    EXPECT_GE(stats.usage.requests, 1);
    EXPECT_GE(stats.usage.input_tokens, 12);
    EXPECT_TRUE(client_.pcache().del(ns, "prompt1"));
    client_.pcache().config_set_max_entries(ns, 1000);
    client_.pcache().config_set_ttl(ns, 3600);
    client_.pcache().config_set_cost_per_hit(ns, 0.002);
    EXPECT_GE(client_.pcache().flush(ns), 0);
    EXPECT_TRUE(client_.pcache().drop(ns));
    client_.pcache().list();
}

// Keyed caches: TOOL.CACHE, NCACHE, GUARD and RERANK each need a license
// with their own feature; without it these skip cleanly.
TEST_F(IntegrationTest, KeyedCacheFamiliesSkipWithoutFullLicense) {
    struct Group {
        const char* feature;
        hotkv::KeyedCacheCommands cmds;
    };
    std::vector<Group> groups{
        {"tool-cache", client_.tool_cache()},
        {"negative-cache", client_.ncache()},
        {"guard-cache", client_.guard()},
        {"rerank-cache", client_.rerank()},
    };

    for (auto& g : groups) {
        std::string ns = key(std::string("keyed_") + g.feature);
        try {
            g.cmds.create(ns);
        } catch (const hotkv::NotLicensedError&) {
            continue; // expected on an unlicensed server
        }
        // If a fuller license is present, exercise the whole family.
        hotkv::Usage usage;
        usage.model = "gpt-4";
        usage.cache_write_1h_tokens = 9;
        g.cmds.set(ns, "k1", "v1", std::nullopt, {}, usage);
        EXPECT_EQ(g.cmds.get(ns, "k1"), "v1");
        auto stats = g.cmds.stats(ns);
        EXPECT_GE(stats.entries, 1);
        EXPECT_GE(stats.usage.requests, 1);
        EXPECT_TRUE(g.cmds.del(ns, "k1"));
        g.cmds.list();
        EXPECT_GE(g.cmds.flush(ns), 0);
        EXPECT_TRUE(g.cmds.drop(ns));
    }
}

// LLMSTATS.* needs a license with the LLM stats feature.
TEST_F(IntegrationTest, LlmStatsFamilySkipsWithoutFullLicense) {
    std::string bucket = key("llmstats");
    hotkv::LlmStatsRecordOptions error_opts;
    error_opts.error = false;
    HOTKV_SKIP_IF_NOT_LICENSED(client_.llmstats().record(bucket, 100, 50, 0, 0.01, 250, error_opts), "llm-stats");

    hotkv::LlmStatsRecordOptions model_opts;
    model_opts.model = "claude-sonnet-5";
    model_opts.cache_write_5m_tokens = 20;
    client_.llmstats().record(bucket, 100, 50, 10, 0, 80, model_opts);

    auto snapshot = client_.llmstats().get(bucket);
    EXPECT_GE(snapshot.requests, 2);
    EXPECT_GE(snapshot.input_tokens, 200);
    EXPECT_GE(snapshot.cache_write_5m_tokens, 20);
    EXPECT_TRUE(client_.llmstats().reset(bucket));
    client_.llmstats().list();
    EXPECT_TRUE(client_.llmstats().drop(bucket));
}

TEST_F(IntegrationTest, LlmStatsPriceSetGetListDel) {
    std::string model = key("model");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.llmstats().price_set(model, 3.0, 15.0), "llm-stats");

    auto price = client_.llmstats().price_get(model);
    EXPECT_EQ(price.source, "custom");
    EXPECT_TRUE(price.priced);
    EXPECT_DOUBLE_EQ(price.input_usd_per_mtok, 3.0);
    EXPECT_DOUBLE_EQ(price.output_usd_per_mtok, 15.0);

    auto all = client_.llmstats().price_list();
    EXPECT_FALSE(all.empty());

    EXPECT_TRUE(client_.llmstats().price_del(model));
    EXPECT_FALSE(client_.llmstats().price_del(model));
}

// LLMBUDGET.* needs a license with the LLM budget feature.
TEST_F(IntegrationTest, LlmBudgetFamilySkipsWithoutFullLicense) {
    std::string name = key("llmbudget");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.llmbudget().set(name, 100000, 5.0, 3600), "llm-budget");

    EXPECT_TRUE(client_.llmbudget().consume(name, 1000, 0.05));

    hotkv::Usage usage;
    usage.model = "gpt-4";
    usage.input_tokens = 10;
    usage.output_tokens = 5;
    EXPECT_TRUE(client_.llmbudget().consume(name, 0, 0.0, usage));

    auto status = client_.llmbudget().status(name);
    EXPECT_GE(status.tokens_used, 1015);
    EXPECT_TRUE(client_.llmbudget().reset(name));
    client_.llmbudget().list();
    EXPECT_TRUE(client_.llmbudget().drop(name));
}

// PROMPT.* needs a license with the prompt-registry feature.
TEST_F(IntegrationTest, PromptRegistryFamilySkipsWithoutFullLicense) {
    std::string name = key("promptreg");
    int64_t version = 0;
    try {
        version = client_.prompt().put(name, "You are a helpful assistant.");
    } catch (const hotkv::NotLicensedError&) {
        GTEST_SKIP() << "server license does not include 'prompt-registry'";
    }
    EXPECT_EQ(version, 1);

    auto got = client_.prompt().get(name);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->content, "You are a helpful assistant.");

    auto latest = client_.prompt().latest(name);
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->version, version);

    client_.prompt().set_active(name, version);
    auto versions = client_.prompt().versions(name);
    EXPECT_EQ(versions, (std::vector<int64_t>{1}));
    client_.prompt().list();
    EXPECT_TRUE(client_.prompt().drop(name));
}

// EMBED.DEDUP.* needs a license with the embed-dedup feature.
TEST_F(IntegrationTest, EmbedDedupFamilySkipsWithoutFullLicense) {
    std::string ns = key("embeddedup");
    bool newly_stored = false;
    try {
        newly_stored = client_.embed_dedup().put(ns, "hash1", "text-embedding-3-small", "0.1,0.2,0.3");
    } catch (const hotkv::NotLicensedError&) {
        GTEST_SKIP() << "server license does not include 'embed-dedup'";
    }
    EXPECT_TRUE(newly_stored);
    EXPECT_FALSE(client_.embed_dedup().put(ns, "hash1", "text-embedding-3-small", "0.1,0.2,0.3"));
    EXPECT_EQ(client_.embed_dedup().get(ns, "hash1"), "0.1,0.2,0.3");
    auto stats = client_.embed_dedup().stats(ns);
    EXPECT_GE(stats.entries, 1);
    EXPECT_TRUE(client_.embed_dedup().release(ns, "hash1"));
    client_.embed_dedup().list();
    client_.embed_dedup().purge(ns);
    EXPECT_TRUE(client_.embed_dedup().drop(ns));
}
