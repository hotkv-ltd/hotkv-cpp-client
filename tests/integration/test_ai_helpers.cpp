// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <hotkv/ai.hpp>

using hotkv::ai::LlmCallResult;
using hotkv::ai::LlmUsage;
using hotkv::ai::PromptCache;
using hotkv::ai::PromptCacheKeyInput;
using hotkv::ai::SemanticCache;
using hotkv::testutil::IntegrationTest;

// PCACHE needs a license with the prompt-cache feature; without it
// PromptCache::create_namespace() raises NotLicensedError and this test
// skips cleanly.
TEST_F(IntegrationTest, PromptCacheGetOrCallSkipsWithoutFullLicense) {
    PromptCache cache(client_, key("promptcachens"), 3600);
    cache.with_stats_bucket(key("promptcachestats"));
    try {
        cache.create_namespace();
    } catch (const hotkv::NotLicensedError&) {
        GTEST_SKIP() << "server license does not include 'prompt-cache'";
    }

    int call_count = 0;
    PromptCacheKeyInput input;
    input.provider = "openai";
    input.model = "gpt-4o";
    input.messages = {{"user", "What is 2+2?"}};
    input.params = {{"temperature", "0.0"}};

    auto fn = [&]() -> LlmCallResult {
        ++call_count;
        LlmUsage usage;
        usage.model = "gpt-4";
        usage.input_tokens = 10;
        usage.output_tokens = 2;
        usage.cost_usd = 0.001;
        usage.latency = std::chrono::milliseconds(50);
        return LlmCallResult{"4", usage};
    };

    auto first = cache.get_or_call(input, fn);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(first.response, "4");
    EXPECT_EQ(call_count, 1);

    // Same logical request (same key): must be a hit, and fn() must NOT
    // be called again.
    auto second = cache.get_or_call(input, fn);
    EXPECT_TRUE(second.hit);
    EXPECT_EQ(second.response, "4");
    EXPECT_EQ(call_count, 1);

    auto stats = cache.stats();
    EXPECT_GE(stats.entries, 1);
    EXPECT_GE(stats.usage.requests, 1);
    EXPECT_GE(stats.usage.input_tokens, 10);
}

// SCACHE needs a license with the semantic-cache feature; without it this
// test skips cleanly.
TEST_F(IntegrationTest, SemanticCacheGetOrCallOnlyCallsLlmOnMiss) {
    SemanticCache cache(client_, key("semcachens"));
    try {
        cache.create_namespace(3, "cosine", 0.8);
    } catch (const hotkv::NotLicensedError&) {
        GTEST_SKIP() << "server license does not include 'semantic-cache'";
    }

    int call_count = 0;
    auto fn = [&]() -> LlmCallResult {
        ++call_count;
        LlmUsage usage;
        usage.model = "gpt-4";
        usage.input_tokens = 10;
        usage.output_tokens = 6;
        return LlmCallResult{"Paris is the capital of France.", usage};
    };

    auto first = cache.get_or_call("capital of france", {1.0, 0.0, 0.0}, fn);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(first.response, "Paris is the capital of France.");
    EXPECT_EQ(call_count, 1);

    // A near-identical embedding vector should hit above the 0.8
    // threshold and must not invoke fn() again.
    auto second = cache.get_or_call("capital of france, once more", {0.99, 0.01, 0.0}, fn);
    EXPECT_TRUE(second.hit);
    EXPECT_EQ(second.response, "Paris is the capital of France.");
    EXPECT_EQ(call_count, 1);

    // A dissimilar vector is a genuine miss: fn() runs again.
    auto third = cache.get_or_call("something unrelated", {0.0, 0.0, 1.0}, fn);
    EXPECT_FALSE(third.hit);
    EXPECT_EQ(call_count, 2);

    auto stats = cache.stats();
    EXPECT_GE(stats.entries, 1);
    EXPECT_GE(stats.usage.requests, 1);
}

TEST_F(IntegrationTest, PromptCacheKeyBuiltFromInputIsUsedConsistently) {
    // Two PromptCacheKeyInput values that are logically identical must
    // hash to the same key and therefore hit the same cache entry, which
    // this exercises indirectly through get_or_call() rather than
    // asserting on the private key string.
    PromptCache cache(client_, key("promptcachekeytest"), std::nullopt);
    try {
        cache.create_namespace();
    } catch (const hotkv::NotLicensedError&) {
        GTEST_SKIP() << "server license does not include 'prompt-cache'";
    }

    PromptCacheKeyInput a;
    a.provider = "anthropic";
    a.model = "claude-sonnet-5";
    a.messages = {{"user", "hi"}};

    PromptCacheKeyInput b = a; // identical by value

    int calls = 0;
    auto fn = [&]() -> LlmCallResult {
        ++calls;
        return LlmCallResult{"hello!", {}};
    };

    cache.get_or_call(a, fn);
    auto second = cache.get_or_call(b, fn);
    EXPECT_TRUE(second.hit);
    EXPECT_EQ(calls, 1);
}
