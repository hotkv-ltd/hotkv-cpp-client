// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/ai.hpp>

#include <gtest/gtest.h>

#include <algorithm>

using hotkv::ai::build_prompt_cache_key;
using hotkv::ai::PromptCacheKeyInput;
using hotkv::ai::PromptMessage;

namespace {
// Deliberately avoids <regex>: GCC's libstdc++ implementation trips a
// spurious -Wmaybe-uninitialized under -O1, and a hex-digest check is
// simple enough to write directly.
bool looks_like_sha256_hex(const std::string& s) {
    if (s.size() != 64) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
} // namespace

// Reference vectors independently computed in Python from the
// canonical form (sorted-key compact JSON,
// SHA-256, lowercase hex), so this pins the C++ SDK to the same wire
// bytes every other official HotKV SDK produces for the same input.
TEST(PromptCacheKey, GoldenVectorTypicalRequest) {
    PromptCacheKeyInput in;
    in.provider = "openai";
    in.model = "gpt-4o";
    in.system = "You are helpful.";
    in.messages = {{"user", "Hello, world!"}, {"assistant", "Hi there!"}};
    // Inserted out of alphabetical order; std::map sorts by key anyway.
    in.params = {{"temperature", "0.7"}, {"max_tokens", "100"}};

    EXPECT_EQ(build_prompt_cache_key(in), "65f3664a9275ca29aa92488c8ef515df7366e11a06f8c4a6c8a04b6f73497f0d");
}

TEST(PromptCacheKey, GoldenVectorEmptySystemAndParamsNonAscii) {
    PromptCacheKeyInput in;
    in.provider = "anthropic";
    in.model = "claude-sonnet-5";
    in.system = "";
    in.messages = {{"user", "caf\xc3\xa9 \xe2\x98\x83 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"}};
    in.params = {};

    EXPECT_EQ(build_prompt_cache_key(in), "e07391eb21976f37496b3d8e51e8ce229fb19d97a645028e1f42b4c69060cc96");
}

TEST(PromptCacheKey, GoldenVectorControlCharacterEscaping) {
    PromptCacheKeyInput in;
    in.provider = "p";
    in.model = "m";
    in.system = "line1\nline2\ttab\"quote\\backslash";
    in.messages = {};
    in.params = {{"a", "1"}};

    EXPECT_EQ(build_prompt_cache_key(in), "a79ca2f70b441a2b493c1972943f277899231bb387d25c09633be4a20437fd0e");
}

TEST(PromptCacheKey, Deterministic) {
    PromptCacheKeyInput in;
    in.provider = "openai";
    in.model = "gpt-4o";
    in.messages = {{"user", "same input"}};
    in.params = {{"z", "1"}, {"a", "2"}};

    EXPECT_EQ(build_prompt_cache_key(in), build_prompt_cache_key(in));
}

TEST(PromptCacheKey, LooksLikeLowercaseHexSha256) {
    PromptCacheKeyInput in;
    in.provider = "openai";
    in.model = "gpt-4o";
    in.messages = {{"user", "hi"}};

    EXPECT_TRUE(looks_like_sha256_hex(build_prompt_cache_key(in)));
}

TEST(PromptCacheKey, DifferentProviderDifferentKey) {
    PromptCacheKeyInput a;
    a.provider = "openai";
    a.model = "gpt-4o";
    a.messages = {{"user", "hi"}};

    PromptCacheKeyInput b = a;
    b.provider = "anthropic";

    EXPECT_NE(build_prompt_cache_key(a), build_prompt_cache_key(b));
}

TEST(PromptCacheKey, MessageOrderMatters) {
    PromptCacheKeyInput a;
    a.provider = "p";
    a.model = "m";
    a.messages = {{"user", "first"}, {"assistant", "second"}};

    PromptCacheKeyInput b = a;
    b.messages = {{"assistant", "second"}, {"user", "first"}};

    EXPECT_NE(build_prompt_cache_key(a), build_prompt_cache_key(b));
}

TEST(PromptCacheKey, ParamValueFormattingMattersButKeyOrderDoesNot) {
    PromptCacheKeyInput a;
    a.provider = "p";
    a.model = "m";
    a.params = {{"temperature", "0.7"}};

    PromptCacheKeyInput b = a;
    // Same logical value, different caller-chosen string formatting:
    // this is deliberately the caller's responsibility, so
    // the keys must differ (byte-identical formatting is what keeps
    // keys shareable across languages, not numeric equivalence).
    b.params = {{"temperature", "0.70"}};

    EXPECT_NE(build_prompt_cache_key(a), build_prompt_cache_key(b));
}
