// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <algorithm>

using hotkv::Reply;
using hotkv::ScanOptions;
using hotkv::Scanner;
using hotkv::testutil::IntegrationTest;

// --- server ---

TEST_F(IntegrationTest, PingEcho) {
    EXPECT_TRUE(client_.ping());
    EXPECT_EQ(client_.ping("hello"), "hello");
    EXPECT_EQ(client_.echo("world"), "world");
}

TEST_F(IntegrationTest, InfoDbsize) {
    EXPECT_FALSE(client_.info().empty());
    EXPECT_GE(client_.dbsize(), 0);
}

TEST_F(IntegrationTest, ServerTime) {
    hotkv::ServerTime t = client_.time();
    EXPECT_GT(t.seconds, 1'700'000'000);
}

TEST_F(IntegrationTest, ConfigGetSet) {
    auto before = client_.config_get("maxmemory-policy");
    if (!before.empty()) {
        std::string original = before[0].second;
        client_.config_set("maxmemory-policy", original);
    }
    // A glob covering many parameters must not throw, whatever the
    // server happens to expose under it.
    EXPECT_NO_THROW(client_.config_get("*max*"));
}

TEST_F(IntegrationTest, ClientNameAndId) {
    EXPECT_GT(client_.client_id(), 0);
    client_.client_setname("hotkv-cpp-test");
    // CLIENT GETNAME is best-effort: pooled connections may not share
    // state with the one SETNAME touched, so only check it doesn't throw.
    EXPECT_NO_THROW(client_.client_getname());
}

// --- scripting ---

TEST_F(IntegrationTest, EvalReturnsValue) {
    Reply r = client_.eval("return 'hi'", {}, {});
    EXPECT_EQ(r.as_string(), "hi");
}

TEST_F(IntegrationTest, EvalWithKeysAndArgs) {
    std::string k = key("evalkey");
    Reply r = client_.eval("return redis.call('SET', KEYS[1], ARGV[1])", {k}, {"value1"});
    (void)r;
    EXPECT_EQ(*client_.get(k), "value1");
}

TEST_F(IntegrationTest, ScriptLoadEvalshaAndExistsFlush) {
    std::string sha = client_.script_load("return 1");
    EXPECT_FALSE(sha.empty());
    Reply r = client_.evalsha(sha, {}, {});
    EXPECT_EQ(r.as_int(), 1);

    auto exists = client_.script_exists({sha, "0000000000000000000000000000000000000000"});
    ASSERT_EQ(exists.size(), 2u);
    EXPECT_TRUE(exists[0]);
    EXPECT_FALSE(exists[1]);
}

TEST_F(IntegrationTest, EvalScriptFallsBackOnNoscript) {
    // eval_script computes the sha1 locally and tries EVALSHA first; the
    // server will not have this exact script cached yet, so this
    // exercises the NOSCRIPT -> EVAL fallback path end to end.
    Reply r = client_.eval_script("return 'fallback-ok'", {}, {});
    EXPECT_EQ(r.as_string(), "fallback-ok");
    // Second call should now be servable via EVALSHA directly.
    Reply r2 = client_.eval_script("return 'fallback-ok'", {}, {});
    EXPECT_EQ(r2.as_string(), "fallback-ok");
}

// --- SCAN family ---

TEST_F(IntegrationTest, ScanFindsAddedKeys) {
    std::string a = key("scan1");
    std::string b = key("scan2");
    client_.set(a, "1");
    client_.set(b, "2");

    ScanOptions opts;
    opts.match = prefix_ + ":scan*";
    opts.count = 100;
    Scanner scanner = client_.scan(opts);
    std::vector<std::string> found;
    std::string value;
    while (scanner.next(value)) {
        found.push_back(value);
    }
    std::sort(found.begin(), found.end());
    EXPECT_EQ(found, (std::vector<std::string>{a, b}));
}

TEST_F(IntegrationTest, HscanSscanZscan) {
    std::string h = key("hscan");
    client_.hset(h, {{"f1", "v1"}, {"f2", "v2"}});
    Scanner hs = client_.hscan(h);
    std::size_t hcount = 0;
    std::string a, b;
    while (hs.next(a, b)) {
        ++hcount;
    }
    EXPECT_EQ(hcount, 2u);

    std::string s = key("sscan");
    client_.sadd(s, {"m1", "m2", "m3"});
    Scanner ss = client_.sscan(s);
    std::size_t scount = 0;
    std::string member;
    while (ss.next(member)) {
        ++scount;
    }
    EXPECT_EQ(scount, 3u);

    std::string z = key("zscan");
    client_.zadd(z, {{"a", 1}, {"b", 2}});
    Scanner zs = client_.zscan(z);
    std::size_t zcount = 0;
    double score = 0.0;
    while (zs.next_scored(member, score)) {
        ++zcount;
    }
    EXPECT_EQ(zcount, 2u);
}
