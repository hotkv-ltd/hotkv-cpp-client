// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

// Live checks, with reply-value assertions, for the typed methods added
// after the first release: GETEX/LCS, OBJECT/EXPIRETIME, LPOS/LMPOP and
// the blocking list/zset pops, the newer hash commands, ZLEXCOUNT/ZMPOP,
// extended XPENDING, BITFIELD_RO, publish/PUBSUB, and server introspection.

#include "test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

using hotkv::testutil::IntegrationTest;
using namespace std::chrono_literals;

// --- strings ---

TEST_F(IntegrationTest, GetExSetsAndClearsTtl) {
    std::string k = key("getex");
    client_.set(k, "v");
    EXPECT_EQ(client_.ttl(k), -1);

    hotkv::GetExOptions ex;
    ex.ex = 100s;
    EXPECT_EQ(client_.getex(k, ex), std::optional<std::string>("v"));
    EXPECT_GT(client_.ttl(k), 0);
    EXPECT_LE(client_.ttl(k), 100);

    hotkv::GetExOptions px;
    px.px = 50000ms;
    EXPECT_EQ(client_.getex(k, px), std::optional<std::string>("v"));
    EXPECT_GT(client_.pttl(k), 40000);

    hotkv::GetExOptions persist;
    persist.persist = true;
    EXPECT_EQ(client_.getex(k, persist), std::optional<std::string>("v"));
    EXPECT_EQ(client_.ttl(k), -1);

    hotkv::GetExOptions exat;
    exat.exat = client_.time().seconds + 1000;
    client_.getex(k, exat);
    EXPECT_GT(client_.ttl(k), 900);

    // No options: plain read, TTL untouched.
    EXPECT_EQ(client_.getex(k), std::optional<std::string>("v"));
    EXPECT_GT(client_.ttl(k), 900);
    EXPECT_FALSE(client_.getex(key("getex-missing")).has_value());
}

TEST_F(IntegrationTest, LcsAndLcsLen) {
    std::string a = key("lcs-a");
    std::string b = key("lcs-b");
    client_.set(a, "ohmytext");
    client_.set(b, "mynewtext");
    EXPECT_EQ(client_.lcs(a, b), "mytext");
    EXPECT_EQ(client_.lcs_len(a, b), 6);
}

// --- keys ---

TEST_F(IntegrationTest, ExpireTimeReportsAbsoluteTime) {
    std::string k = key("expiretime");
    client_.set(k, "v");
    EXPECT_EQ(client_.expiretime(k), -1);
    EXPECT_EQ(client_.pexpiretime(k), -1);
    EXPECT_EQ(client_.expiretime(key("expiretime-missing")), -2);

    int64_t now = client_.time().seconds;
    client_.expire(k, 100);
    int64_t at = client_.expiretime(k);
    EXPECT_GE(at, now + 99);
    EXPECT_LE(at, now + 102);
    int64_t pat = client_.pexpiretime(k);
    EXPECT_GE(pat, at * 1000 - 1000);
    EXPECT_LE(pat, at * 1000 + 1000);
}

TEST_F(IntegrationTest, ObjectIntrospection) {
    std::string k = key("object");
    client_.set(k, "v");
    auto refcount = client_.object_refcount(k);
    ASSERT_TRUE(refcount.has_value());
    EXPECT_GE(*refcount, 1);
    auto idle = client_.object_idletime(k);
    ASSERT_TRUE(idle.has_value());
    EXPECT_GE(*idle, 0);
    EXPECT_LE(*idle, 5);
    EXPECT_FALSE(client_.object_idletime(key("object-missing")).has_value());
    EXPECT_FALSE(client_.object_refcount(key("object-missing")).has_value());

    // FREQ is only tracked under an LFU maxmemory-policy; otherwise the
    // server refuses with an explanatory error.
    try {
        auto freq = client_.object_freq(k);
        ASSERT_TRUE(freq.has_value());
        EXPECT_GE(*freq, 0);
    } catch (const hotkv::ServerError& e) {
        EXPECT_NE(std::string(e.what()).find("LFU"), std::string::npos) << e.what();
    }
}

// --- hashes ---

TEST_F(IntegrationTest, HashFieldExpireTime) {
    std::string k = key("hexpiretime");
    client_.hset(k, {{"a", "1"}, {"b", "2"}});
    int64_t now = client_.time().seconds;
    client_.hexpire(k, 100, {"a"});
    auto secs = client_.hexpiretime(k, {"a", "b", "missing"});
    ASSERT_EQ(secs.size(), 3u);
    EXPECT_GE(secs[0], now + 99);
    EXPECT_LE(secs[0], now + 102);
    EXPECT_EQ(secs[1], -1);
    EXPECT_EQ(secs[2], -2);
    auto ms = client_.hpexpiretime(k, {"a", "b"});
    ASSERT_EQ(ms.size(), 2u);
    EXPECT_GE(ms[0], secs[0] * 1000 - 1000);
    EXPECT_LE(ms[0], secs[0] * 1000 + 1000);
    EXPECT_EQ(ms[1], -1);
}

TEST_F(IntegrationTest, HGetDelReturnsAndRemovesFields) {
    std::string k = key("hgetdel");
    client_.hset(k, {{"a", "1"}, {"b", "2"}, {"c", "3"}});
    auto got = client_.hgetdel(k, {"a", "nope", "c"});
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0], std::optional<std::string>("1"));
    EXPECT_FALSE(got[1].has_value());
    EXPECT_EQ(got[2], std::optional<std::string>("3"));
    EXPECT_EQ(client_.hlen(k), 1);
    EXPECT_TRUE(client_.hexists(k, "b"));
}

TEST_F(IntegrationTest, HGetExReturnsValuesAndAppliesTtl) {
    std::string k = key("hgetex");
    client_.hset(k, {{"a", "1"}, {"b", "2"}});
    hotkv::GetExOptions ex;
    ex.ex = 100s;
    auto got = client_.hgetex(k, {"a", "missing"}, ex);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], std::optional<std::string>("1"));
    EXPECT_FALSE(got[1].has_value());
    auto ttl = client_.httl(k, {"a", "b"});
    EXPECT_GT(ttl[0], 90);
    EXPECT_LE(ttl[0], 100);
    EXPECT_EQ(ttl[1], -1);

    hotkv::GetExOptions persist;
    persist.persist = true;
    client_.hgetex(k, {"a"}, persist);
    EXPECT_EQ(client_.httl(k, {"a"})[0], -1);

    // No options: a plain multi-get.
    auto plain = client_.hgetex(k, {"b"});
    ASSERT_EQ(plain.size(), 1u);
    EXPECT_EQ(plain[0], std::optional<std::string>("2"));
}

TEST_F(IntegrationTest, HSetExHonoursFnxFxxAndTtl) {
    std::string k = key("hsetex");
    hotkv::HSetExOptions fnx;
    fnx.fnx = true;
    fnx.ex = 100s;
    EXPECT_TRUE(client_.hsetex(k, {{"a", "1"}, {"b", "2"}}, fnx));
    EXPECT_EQ(client_.hget(k, "a"), std::optional<std::string>("1"));
    auto ttl = client_.httl(k, {"a", "b"});
    EXPECT_GT(ttl[0], 90);
    EXPECT_GT(ttl[1], 90);
    // FNX again: the fields now exist, nothing is written.
    EXPECT_FALSE(client_.hsetex(k, {{"a", "changed"}}, fnx));
    EXPECT_EQ(client_.hget(k, "a"), std::optional<std::string>("1"));

    hotkv::HSetExOptions fxx;
    fxx.fxx = true;
    fxx.keepttl = true;
    EXPECT_TRUE(client_.hsetex(k, {{"a", "2"}}, fxx));
    EXPECT_EQ(client_.hget(k, "a"), std::optional<std::string>("2"));
    EXPECT_GT(client_.httl(k, {"a"})[0], 90);
    // FXX with a missing field: refused.
    EXPECT_FALSE(client_.hsetex(k, {{"zzz", "1"}}, fxx));
    EXPECT_FALSE(client_.hexists(k, "zzz"));
}

// --- lists ---

TEST_F(IntegrationTest, LposFindsMatchesWithRankAndMaxlen) {
    std::string k = key("lpos");
    client_.rpush(k, {"a", "b", "c", "b", "d"});
    EXPECT_EQ(client_.lpos(k, "b"), std::optional<int64_t>(1));
    EXPECT_EQ(client_.lpos(k, "b", 2), std::optional<int64_t>(3));
    EXPECT_EQ(client_.lpos(k, "b", -1), std::optional<int64_t>(3));
    EXPECT_FALSE(client_.lpos(k, "zzz").has_value());
    EXPECT_FALSE(client_.lpos(k, "d", std::nullopt, 2).has_value()); // beyond MAXLEN 2
}

TEST_F(IntegrationTest, LmpopPopsFromTheFirstNonEmptyList) {
    std::string empty = key("lmpop-empty");
    std::string full = key("lmpop-full");
    client_.rpush(full, {"a", "b", "c"});
    auto res = client_.lmpop({empty, full}, /*left=*/false, 2);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->key, full);
    EXPECT_EQ(res->values, (std::vector<std::string>{"c", "b"}));
    auto left = client_.lmpop({empty, full}, true);
    ASSERT_TRUE(left.has_value());
    EXPECT_EQ(left->values, (std::vector<std::string>{"a"}));
    EXPECT_FALSE(client_.lmpop({empty, full}, true).has_value());
}

TEST_F(IntegrationTest, BlockingListPopsReturnImmediatelyWhenDataExists) {
    std::string k1 = key("bl1");
    std::string k2 = key("bl2");
    client_.rpush(k2, {"x", "y"});
    auto l = client_.blpop({k1, k2}, 1000ms);
    ASSERT_TRUE(l.has_value());
    EXPECT_EQ(l->first, k2);
    EXPECT_EQ(l->second, "x");
    auto r = client_.brpop({k1, k2}, 1000ms);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->second, "y");
}

TEST_F(IntegrationTest, BlockingListPopsTimeOutWithNullopt) {
    std::string k = key("bl-empty");
    auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(client_.blpop({k}, 200ms).has_value());
    EXPECT_FALSE(client_.brpop({k}, 200ms).has_value());
    EXPECT_FALSE(client_.blmove(k, key("bl-dst"), true, false, 100ms).has_value());
    EXPECT_FALSE(client_.brpoplpush(k, key("bl-dst"), 100ms).has_value());
    EXPECT_FALSE(client_.blmpop(100ms, {k}, true).has_value());
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, 600ms);
    EXPECT_LT(elapsed, 5000ms);
}

TEST_F(IntegrationTest, BlpopWakesWhenAnotherClientPushes) {
    std::string k = key("bl-wake");
    std::thread pusher([&] {
        std::this_thread::sleep_for(150ms);
        client_.rpush(k, {"woke"});
    });
    auto got = client_.blpop({k}, 5000ms);
    pusher.join();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->second, "woke");
}

TEST_F(IntegrationTest, BlockingTimeoutLongerThanCommandTimeoutDoesNotTimeOutTheSocket) {
    hotkv::Options opts = client_.options();
    opts.command_timeout = 300ms;
    hotkv::Client tight(opts);
    std::string k = key("bl-long");
    // Blocks 1s server side, well past the 300ms socket timeout.
    EXPECT_FALSE(tight.blpop({k}, 1000ms).has_value());
}

TEST_F(IntegrationTest, BlmoveBrpoplpushBlmpopMoveData) {
    std::string src = key("blm-src");
    std::string dst = key("blm-dst");
    client_.rpush(src, {"1", "2", "3", "4"});
    EXPECT_EQ(client_.blmove(src, dst, /*src_left=*/true, /*dst_left=*/false, 500ms), std::optional<std::string>("1"));
    EXPECT_EQ(client_.brpoplpush(src, dst, 500ms), std::optional<std::string>("4"));
    EXPECT_EQ(client_.lrange(dst, 0, -1), (std::vector<std::string>{"4", "1"}));
    auto res = client_.blmpop(500ms, {src}, true, 5);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->key, src);
    EXPECT_EQ(res->values, (std::vector<std::string>{"2", "3"}));
}

// --- sorted sets ---

TEST_F(IntegrationTest, ZLexCountCountsRange) {
    std::string k = key("zlex");
    client_.zadd(k, {{"a", 0}, {"b", 0}, {"c", 0}, {"d", 0}});
    EXPECT_EQ(client_.zlexcount(k, "-", "+"), 4);
    EXPECT_EQ(client_.zlexcount(k, "[b", "(d"), 2);
    EXPECT_EQ(client_.zlexcount(k, "[x", "+"), 0);
}

TEST_F(IntegrationTest, ZmpopPopsMinOrMaxFromFirstNonEmpty) {
    std::string empty = key("zmpop-empty");
    std::string z = key("zmpop");
    client_.zadd(z, {{"a", 1}, {"b", 2}, {"c", 3}});
    auto mx = client_.zmpop({empty, z}, /*min=*/false, 2);
    ASSERT_TRUE(mx.has_value());
    EXPECT_EQ(mx->key, z);
    ASSERT_EQ(mx->members.size(), 2u);
    EXPECT_EQ(mx->members[0].member, "c");
    EXPECT_DOUBLE_EQ(mx->members[0].score, 3.0);
    EXPECT_EQ(mx->members[1].member, "b");
    auto mn = client_.zmpop({empty, z}, true);
    ASSERT_TRUE(mn.has_value());
    ASSERT_EQ(mn->members.size(), 1u);
    EXPECT_EQ(mn->members[0].member, "a");
    EXPECT_FALSE(client_.zmpop({empty, z}, true).has_value());
}

TEST_F(IntegrationTest, BlockingZsetPops) {
    std::string empty = key("bz-empty");
    std::string z = key("bz");
    client_.zadd(z, {{"a", 1.5}, {"b", 2.5}, {"c", 3.5}});
    auto lo = client_.bzpopmin({empty, z}, 500ms);
    ASSERT_TRUE(lo.has_value());
    EXPECT_EQ(lo->key, z);
    EXPECT_EQ(lo->member, "a");
    EXPECT_DOUBLE_EQ(lo->score, 1.5);
    auto hi = client_.bzpopmax({empty, z}, 500ms);
    ASSERT_TRUE(hi.has_value());
    EXPECT_EQ(hi->member, "c");
    EXPECT_DOUBLE_EQ(hi->score, 3.5);
    auto many = client_.bzmpop(500ms, {empty, z}, true, 5);
    ASSERT_TRUE(many.has_value());
    ASSERT_EQ(many->members.size(), 1u);
    EXPECT_EQ(many->members[0].member, "b");

    EXPECT_FALSE(client_.bzpopmin({empty}, 100ms).has_value());
    EXPECT_FALSE(client_.bzpopmax({empty}, 100ms).has_value());
    EXPECT_FALSE(client_.bzmpop(100ms, {empty}, false).has_value());
}

// --- streams ---

TEST_F(IntegrationTest, XPendingRangeListsPendingEntries) {
    std::string s = key("xpending");
    client_.xadd(s, {{"f", "1"}}, "1-0");
    client_.xadd(s, {{"f", "2"}}, "2-0");
    client_.xgroup_create(s, "g", "0");
    client_.xreadgroup("g", "alice", {{s, ">"}}, 1);
    client_.xreadgroup("g", "bob", {{s, ">"}}, 1);

    auto all = client_.xpending_range(s, "g", "-", "+", 10);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].id, "1-0");
    EXPECT_EQ(all[0].consumer, "alice");
    EXPECT_GE(all[0].idle_ms, 0);
    EXPECT_EQ(all[0].delivery_count, 1);
    EXPECT_EQ(all[1].consumer, "bob");

    auto only_bob = client_.xpending_range(s, "g", "-", "+", 10, std::string("bob"));
    ASSERT_EQ(only_bob.size(), 1u);
    EXPECT_EQ(only_bob[0].id, "2-0");

    auto idle = client_.xpending_range(s, "g", "-", "+", 10, std::nullopt, 3600000);
    EXPECT_TRUE(idle.empty());
}

TEST_F(IntegrationTest, XReadBlockZeroWaitsLongerThanTheSocketTimeout) {
    hotkv::Options opts = client_.options();
    opts.command_timeout = 300ms;
    hotkv::Client tight(opts);
    std::string s = key("xread-block0");
    std::thread adder([&] {
        std::this_thread::sleep_for(800ms);
        client_.xadd(s, {{"f", "v"}});
    });
    // BLOCK 0 used to inherit a fixed 5 s socket timeout; it must wait for data.
    auto res = tight.xread({{s, "$"}}, 0, 0);
    adder.join();
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].entries.size(), 1u);
}

// --- bitmaps ---

TEST_F(IntegrationTest, BitfieldRoReadsOnly) {
    std::string k = key("bitfield-ro");
    client_.bitfield(k, {"SET", "u8", "0", "200"});
    auto got = client_.bitfield_ro(k, {"GET", "u8", "0", "GET", "u4", "0"});
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], std::optional<int64_t>(200));
    EXPECT_EQ(got[1], std::optional<int64_t>(12)); // 200 = 0b11001000
    EXPECT_THROW(client_.bitfield_ro(k, {"SET", "u8", "0", "1"}), hotkv::ServerError);
}

// --- scripting ---

TEST_F(IntegrationTest, EvalRoAndEvalshaRo) {
    std::string k = key("evalro");
    client_.set(k, "payload");
    EXPECT_EQ(client_.eval_ro("return redis.call('GET', KEYS[1])", {k}, {}).as_string(), "payload");
    std::string sha = client_.script_load("return ARGV[1]");
    EXPECT_EQ(client_.evalsha_ro(sha, {}, {"echoed"}).as_string(), "echoed");
    EXPECT_THROW(client_.eval_ro("return redis.call('SET', KEYS[1], 'x')", {k}, {}), hotkv::ServerError);
}

// --- pub/sub, publishing side ---

TEST_F(IntegrationTest, PublishReportsReceiversAndSubscriberGetsTheMessage) {
    std::string channel = key("pub");
    EXPECT_EQ(client_.publish(channel, "nobody"), 0);

    hotkv::Subscriber sub = client_.create_subscriber();
    sub.subscribe({channel});
    EXPECT_EQ(client_.publish(channel, "hello typed publish"), 1);
    auto msg = sub.next_message(2000ms);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->kind, "message");
    EXPECT_EQ(msg->channel, channel);
    EXPECT_EQ(msg->payload, "hello typed publish");

    // Binary safe payload.
    std::string binary("a\0b\r\nc", 6);
    EXPECT_EQ(client_.publish(channel, binary), 1);
    auto bin = sub.next_message(2000ms);
    ASSERT_TRUE(bin.has_value());
    EXPECT_EQ(bin->payload, binary);
}

TEST_F(IntegrationTest, PubsubIntrospection) {
    std::string c1 = key("ps1");
    std::string c2 = key("ps2");
    std::string pattern = prefix_ + ":glob*";
    hotkv::Subscriber sub = client_.create_subscriber();
    hotkv::Subscriber sub2 = client_.create_subscriber();
    sub.subscribe({c1, c2});
    sub2.subscribe({c1});
    sub.psubscribe({pattern});

    auto channels = client_.pubsub_channels(prefix_ + ":*");
    std::sort(channels.begin(), channels.end());
    std::vector<std::string> want{c1, c2};
    std::sort(want.begin(), want.end());
    EXPECT_EQ(channels, want);

    auto numsub = client_.pubsub_numsub({c1, c2, key("ps-none")});
    ASSERT_EQ(numsub.size(), 3u);
    EXPECT_EQ(numsub[0], (std::pair<std::string, int64_t>{c1, 2}));
    EXPECT_EQ(numsub[1], (std::pair<std::string, int64_t>{c2, 1}));
    EXPECT_EQ(numsub[2].second, 0);

    EXPECT_GE(client_.pubsub_numpat(), 1);
    EXPECT_TRUE(client_.pubsub_channels(prefix_ + ":nomatch*").empty());
}

TEST_F(IntegrationTest, ShardPublishAndIntrospection) {
    std::string channel = key("shard-pub");
    hotkv::Subscriber sub = client_.create_subscriber();
    sub.ssubscribe({channel});
    EXPECT_EQ(client_.spublish(channel, "shard msg"), 1);
    auto msg = sub.next_message(2000ms);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->kind, "smessage");
    EXPECT_EQ(msg->payload, "shard msg");
    auto channels = client_.pubsub_shardchannels(channel);
    EXPECT_EQ(channels, (std::vector<std::string>{channel}));
    auto numsub = client_.pubsub_shardnumsub({channel});
    ASSERT_EQ(numsub.size(), 1u);
    EXPECT_EQ(numsub[0].second, 1);
}

TEST_F(IntegrationTest, MessagesPublishedDuringSubscribeConfirmationAreNotLost) {
    std::string c1 = key("race1");
    hotkv::Subscriber sub = client_.create_subscriber();
    sub.subscribe({c1});
    // Flood the first channel while a second subscribe is in flight: every
    // message that overtakes the confirmation must still be delivered.
    std::thread flood([&] {
        for (int i = 0; i < 200; ++i) {
            client_.publish(c1, std::to_string(i));
        }
    });
    for (int i = 0; i < 20; ++i) {
        sub.subscribe({key("race-extra" + std::to_string(i))});
    }
    flood.join();
    int received = 0;
    while (auto m = sub.next_message(300ms)) {
        EXPECT_EQ(m->payload, std::to_string(received));
        ++received;
    }
    EXPECT_EQ(received, 200);
}

// --- server introspection ---

TEST_F(IntegrationTest, ServerIntrospectionCommands) {
    EXPECT_GT(client_.lastsave(), 1600000000);

    std::string k = key("memusage");
    client_.set(k, std::string(1000, 'x'));
    auto usage = client_.memory_usage(k);
    ASSERT_TRUE(usage.has_value());
    EXPECT_GE(*usage, 1000);
    EXPECT_FALSE(client_.memory_usage(key("memusage-missing")).has_value());

    EXPECT_GT(client_.command_count(), 100);
    client_.config_resetstat();

    client_.slowlog_reset();
    EXPECT_EQ(client_.slowlog_len(), 0);
    hotkv::Reply log = client_.slowlog_get(5);
    EXPECT_TRUE(log.is_array_like());
    EXPECT_TRUE(log.as_array().empty());
}

TEST_F(IntegrationTest, SwapDbSwapsKeyspaces) {
    // SWAPDB between two otherwise unused databases; db 14 and 15 are
    // emptied by the test itself (the shared dev server keeps data in 0).
    hotkv::Options o14 = client_.options();
    o14.database = 14;
    hotkv::Client c14(o14);
    hotkv::Options o15 = client_.options();
    o15.database = 15;
    hotkv::Client c15(o15);
    std::string k = key("swapdb");
    c14.set(k, "in14");
    c14.swapdb(14, 15);
    EXPECT_FALSE(c14.get(k).has_value());
    EXPECT_EQ(c15.get(k), std::optional<std::string>("in14"));
    c15.swapdb(14, 15);
    EXPECT_EQ(c14.get(k), std::optional<std::string>("in14"));
    c14.del({k});
}

TEST_F(IntegrationTest, ClusterIntrospectionOnAStandaloneNode) {
    EXPECT_EQ(client_.cluster_myid().size(), 40u);
    EXPECT_NE(client_.cluster_info().find("cluster_enabled:"), std::string::npos);
    EXPECT_NE(client_.cluster_nodes().find("myself"), std::string::npos);
    EXPECT_TRUE(client_.cluster_slots().is_array_like());
    EXPECT_EQ(client_.cluster_keyslot("foo"), 12182);
    EXPECT_EQ(client_.cluster_keyslot("{user1000}.following"), 3443);
    EXPECT_EQ(client_.cluster_countkeysinslot(0), 0);
    EXPECT_TRUE(client_.cluster_getkeysinslot(0, 5).empty());
}

// --- typed methods that only had indirect coverage ---

TEST_F(IntegrationTest, StringExpiryShortcutsAndSetnx) {
    std::string a = key("setex");
    client_.setex(a, 100, "v1");
    EXPECT_EQ(client_.get(a), std::optional<std::string>("v1"));
    EXPECT_GT(client_.ttl(a), 90);

    std::string b = key("psetex");
    client_.psetex(b, 60000, "v2");
    EXPECT_EQ(client_.get(b), std::optional<std::string>("v2"));
    EXPECT_GT(client_.pttl(b), 50000);
    EXPECT_LE(client_.pttl(b), 60000);

    std::string c = key("setnx");
    EXPECT_TRUE(client_.setnx(c, "first"));
    EXPECT_FALSE(client_.setnx(c, "second"));
    EXPECT_EQ(client_.get(c), std::optional<std::string>("first"));
}

TEST_F(IntegrationTest, ExpireVariantsAndMove) {
    std::string k = key("pexpire");
    client_.set(k, "v");
    EXPECT_TRUE(client_.pexpire(k, 90000));
    EXPECT_GT(client_.pttl(k), 80000);
    EXPECT_TRUE(client_.expireat(k, client_.time().seconds + 500));
    EXPECT_GT(client_.ttl(k), 400);
    EXPECT_FALSE(client_.pexpire(key("pexpire-missing"), 1000));
    EXPECT_FALSE(client_.expireat(key("pexpire-missing"), client_.time().seconds + 5));

    // MOVE to database 14 (own server only: the key is removed again).
    EXPECT_TRUE(client_.move(k, 14));
    EXPECT_FALSE(client_.get(k).has_value());
    hotkv::Options o14 = client_.options();
    o14.database = 14;
    hotkv::Client c14(o14);
    EXPECT_EQ(c14.get(k), std::optional<std::string>("v"));
    c14.del({k});
    EXPECT_FALSE(client_.move(key("move-missing"), 14));
}

TEST_F(IntegrationTest, HmsetAndZremWork) {
    std::string h = key("hmset");
    client_.hmset(h, {{"a", "1"}, {"b", "2"}});
    EXPECT_EQ(client_.hgetall(h).size(), 2u);
    EXPECT_EQ(client_.hget(h, "b"), std::optional<std::string>("2"));

    std::string z = key("zrem");
    client_.zadd(z, {{"a", 1}, {"b", 2}, {"c", 3}});
    EXPECT_EQ(client_.zrem(z, {"a", "zzz"}), 1);
    EXPECT_EQ(client_.zcard(z), 2);
    EXPECT_FALSE(client_.zscore(z, "a").has_value());
}

TEST_F(IntegrationTest, ScriptFlushDropsLoadedScripts) {
    std::string sha = client_.script_load("return 'flush-me'");
    EXPECT_EQ(client_.script_exists({sha}), (std::vector<bool>{true}));
    client_.script_flush();
    EXPECT_EQ(client_.script_exists({sha}), (std::vector<bool>{false}));
    // eval_script re-primes the cache transparently after a flush.
    EXPECT_EQ(client_.eval_script("return 'flush-me'", {}, {}).as_string(), "flush-me");
}

TEST_F(IntegrationTest, FlushDbClearsOnlyThatDatabase) {
    hotkv::Options o13 = client_.options();
    o13.database = 13;
    hotkv::Client c13(o13);
    c13.set("a", "1");
    c13.set("b", "2");
    EXPECT_GE(c13.dbsize(), 2);
    std::string k = key("flushdb-survivor");
    client_.set(k, "stays");
    c13.flushdb();
    EXPECT_EQ(c13.dbsize(), 0);
    c13.set("c", "3");
    c13.flushdb(/*async=*/true);
    // Async flush completes in the background; poll briefly.
    for (int i = 0; i < 50 && c13.dbsize() != 0; ++i) {
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_EQ(c13.dbsize(), 0);
    EXPECT_EQ(client_.get(k), std::optional<std::string>("stays"));
}

// FLUSHALL wipes the whole server, so it only runs when explicitly allowed
// (a throwaway test server): HOTKV_TEST_ALLOW_FLUSHALL=1.
TEST_F(IntegrationTest, FlushAllEmptiesEveryDatabase) {
    const char* allow = std::getenv("HOTKV_TEST_ALLOW_FLUSHALL");
    if (allow == nullptr || std::string(allow) != "1") {
        GTEST_SKIP() << "set HOTKV_TEST_ALLOW_FLUSHALL=1 to run FLUSHALL against the test server";
    }
    hotkv::Options o12 = client_.options();
    o12.database = 12;
    hotkv::Client c12(o12);
    client_.set(key("flushall-0"), "x");
    c12.set("y", "1");
    client_.flushall();
    EXPECT_EQ(client_.dbsize(), 0);
    EXPECT_EQ(c12.dbsize(), 0);
    c12.set("y", "1");
    client_.flushall(/*async=*/true);
    for (int i = 0; i < 50 && c12.dbsize() != 0; ++i) {
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_EQ(c12.dbsize(), 0);
}

TEST_F(IntegrationTest, HashFieldExpireVariantsSetAbsoluteAndMillisecondTtls) {
    std::string k = key("hexpire-variants");
    client_.hset(k, {{"a", "1"}, {"b", "2"}, {"c", "3"}, {"d", "4"}});
    int64_t now = client_.time().seconds;
    EXPECT_EQ(client_.hpexpire(k, 90000, {"a"}), (std::vector<int64_t>{1}));
    EXPECT_GT(client_.hpttl(k, {"a"})[0], 80000);
    EXPECT_EQ(client_.hexpireat(k, now + 500, {"b"}), (std::vector<int64_t>{1}));
    EXPECT_GT(client_.httl(k, {"b"})[0], 400);
    EXPECT_EQ(client_.hpexpireat(k, (now + 700) * 1000, {"c"}), (std::vector<int64_t>{1}));
    EXPECT_GT(client_.httl(k, {"c"})[0], 600);
    // Conditions: NX refuses a field that already has a TTL, XX needs one.
    EXPECT_EQ(client_.hpexpire(k, 1000, {"a"}, hotkv::ExpireCondition::Nx), (std::vector<int64_t>{0}));
    EXPECT_EQ(client_.hpexpire(k, 1000, {"d"}, hotkv::ExpireCondition::Xx), (std::vector<int64_t>{0}));
    EXPECT_EQ(client_.hpexpire(k, 1000, {"missing"}), (std::vector<int64_t>{-2}));
}

TEST_F(IntegrationTest, UnsubscribeVariantsStopDelivery) {
    std::string chan = key("unsub-chan");
    std::string pat = prefix_ + ":unsub-pat.*";
    std::string shard = key("unsub-shard");
    hotkv::Subscriber sub = client_.create_subscriber();
    sub.subscribe({chan});
    sub.psubscribe({pat});
    sub.ssubscribe({shard});

    sub.unsubscribe({chan});
    EXPECT_EQ(client_.publish(chan, "gone"), 0);
    sub.punsubscribe({pat});
    EXPECT_EQ(client_.pubsub_numpat() >= 0, true);
    EXPECT_EQ(client_.publish(prefix_ + ":unsub-pat.x", "gone"), 0);
    sub.sunsubscribe({shard});
    EXPECT_EQ(client_.spublish(shard, "gone"), 0);
    EXPECT_FALSE(sub.next_message(200ms).has_value());

    // Unsubscribing from everything of a kind with an empty list.
    sub.subscribe({chan, key("unsub-chan2")});
    sub.unsubscribe();
    EXPECT_EQ(client_.publish(chan, "gone"), 0);
}

TEST_F(IntegrationTest, ConfigSetChangesTheValueConfigGetReports) {
    auto before = client_.config_get("slowlog-max-len");
    ASSERT_EQ(before.size(), 1u);
    std::string original = before[0].second;
    std::string changed = original == "321" ? "322" : "321";
    client_.config_set("slowlog-max-len", changed);
    auto after = client_.config_get("slowlog-max-len");
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0].second, changed);
    client_.config_set("slowlog-max-len", original);
    EXPECT_EQ(client_.config_get("slowlog-max-len")[0].second, original);
}

TEST_F(IntegrationTest, ClientSetNameIsReportedByGetNameOnTheSameConnection) {
    hotkv::Options one = client_.options();
    one.pool_size = 1; // every call reuses the one connection
    hotkv::Client c(one);
    EXPECT_FALSE(c.client_getname().has_value());
    c.client_setname("hotkv-cpp-named");
    EXPECT_EQ(c.client_getname(), std::optional<std::string>("hotkv-cpp-named"));
    hotkv::Options named = client_.options();
    named.client_name = "from-options";
    EXPECT_EQ(hotkv::Client(named).client_getname(), std::optional<std::string>("from-options"));
}
