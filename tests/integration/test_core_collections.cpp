// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <algorithm>

using hotkv::ScoredMember;
using hotkv::ZRangeBy;
using hotkv::ZRangeOptions;
using hotkv::testutil::IntegrationTest;

// --- lists ---

TEST_F(IntegrationTest, ListPushPopRange) {
    std::string k = key("list");
    EXPECT_EQ(client_.rpush(k, {"a", "b", "c"}), 3);
    EXPECT_EQ(client_.lpush(k, {"z"}), 4);
    EXPECT_EQ(client_.llen(k), 4);
    EXPECT_EQ(client_.lrange(k, 0, -1), (std::vector<std::string>{"z", "a", "b", "c"}));
    EXPECT_EQ(*client_.lpop(k), "z");
    EXPECT_EQ(*client_.rpop(k), "c");
    EXPECT_EQ(client_.lrange(k, 0, -1), (std::vector<std::string>{"a", "b"}));
}

TEST_F(IntegrationTest, ListPushxOnMissingKeyIsNoop) {
    std::string k = key("listx");
    EXPECT_EQ(client_.lpushx(k, {"a"}), 0);
    EXPECT_EQ(client_.rpushx(k, {"a"}), 0);
    EXPECT_EQ(client_.exists({k}), 0);
}

TEST_F(IntegrationTest, ListTrimIndexSetRemInsert) {
    std::string k = key("listops");
    client_.rpush(k, {"a", "b", "c", "d", "e"});
    client_.ltrim(k, 1, 3);
    EXPECT_EQ(client_.lrange(k, 0, -1), (std::vector<std::string>{"b", "c", "d"}));
    EXPECT_EQ(*client_.lindex(k, 0), "b");
    EXPECT_TRUE(client_.lset(k, 0, "B"));
    EXPECT_EQ(*client_.lindex(k, 0), "B");
    client_.rpush(k, {"c"});
    EXPECT_EQ(client_.lrem(k, 1, "c"), 1);
    // linsert_before() mutates the list and returns its new length;
    // reading llen() separately (not as a second, unordered argument to
    // the same comparison) avoids relying on unspecified evaluation
    // order between the two calls.
    int64_t new_length = client_.linsert_before(k, "d", "c2");
    EXPECT_EQ(new_length, client_.llen(k));
}

TEST_F(IntegrationTest, ListMoveAndRpoplpush) {
    std::string src = key("movesrc");
    std::string dst = key("movedst");
    client_.rpush(src, {"a", "b", "c"});
    EXPECT_EQ(*client_.lmove(src, dst, false, true), "c");
    EXPECT_EQ(*client_.rpoplpush(src, dst), "b");
    EXPECT_EQ(client_.lrange(dst, 0, -1), (std::vector<std::string>{"b", "c"}));
}

TEST_F(IntegrationTest, ListPopWithCount) {
    std::string k = key("listcount");
    client_.rpush(k, {"a", "b", "c", "d"});
    EXPECT_EQ(client_.lpop(k, 2), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(client_.rpop(k, 2), (std::vector<std::string>{"d", "c"}));
}

// --- sets ---

TEST_F(IntegrationTest, SetAddRemMembers) {
    std::string k = key("set");
    EXPECT_EQ(client_.sadd(k, {"a", "b", "c"}), 3);
    EXPECT_EQ(client_.sadd(k, {"a"}), 0);
    EXPECT_EQ(client_.scard(k), 3);
    EXPECT_TRUE(client_.sismember(k, "a"));
    EXPECT_FALSE(client_.sismember(k, "z"));
    auto flags = client_.smismember(k, {"a", "z"});
    EXPECT_EQ(flags, (std::vector<bool>{true, false}));
    EXPECT_EQ(client_.srem(k, {"a"}), 1);
    auto members = client_.smembers(k);
    std::sort(members.begin(), members.end());
    EXPECT_EQ(members, (std::vector<std::string>{"b", "c"}));
}

TEST_F(IntegrationTest, SetPopRandmember) {
    std::string k = key("setpop");
    client_.sadd(k, {"a", "b", "c"});
    EXPECT_TRUE(client_.spop(k).has_value());
    EXPECT_EQ(client_.scard(k), 2);
    EXPECT_EQ(client_.srandmember(k, 10).size(), 2u);
    EXPECT_TRUE(client_.srandmember(k).has_value());
}

TEST_F(IntegrationTest, SetAlgebra) {
    std::string a = key("seta");
    std::string b = key("setb");
    std::string dst = key("setdst");
    client_.sadd(a, {"x", "y", "z"});
    client_.sadd(b, {"y", "z", "w"});

    auto inter = client_.sinter({a, b});
    std::sort(inter.begin(), inter.end());
    EXPECT_EQ(inter, (std::vector<std::string>{"y", "z"}));
    EXPECT_EQ(client_.sinterstore(dst, {a, b}), 2);
    EXPECT_EQ(client_.sintercard({a, b}), 2);

    auto uni = client_.sunion({a, b});
    EXPECT_EQ(uni.size(), 4u);
    EXPECT_EQ(client_.sunionstore(dst, {a, b}), 4);

    auto diff = client_.sdiff({a, b});
    EXPECT_EQ(diff, (std::vector<std::string>{"x"}));
    EXPECT_EQ(client_.sdiffstore(dst, {a, b}), 1);
}

TEST_F(IntegrationTest, SetMove) {
    std::string a = key("smovea");
    std::string b = key("smoveb");
    client_.sadd(a, {"x"});
    EXPECT_TRUE(client_.smove(a, b, "x"));
    EXPECT_FALSE(client_.sismember(a, "x"));
    EXPECT_TRUE(client_.sismember(b, "x"));
}

// --- hashes ---

TEST_F(IntegrationTest, HashBasics) {
    std::string k = key("hash");
    EXPECT_TRUE(client_.hset(k, "f1", "v1"));
    EXPECT_FALSE(client_.hset(k, "f1", "v1b")); // field already existed
    EXPECT_EQ(client_.hset(k, {{"f2", "v2"}, {"f3", "v3"}}), 2);
    EXPECT_EQ(*client_.hget(k, "f1"), "v1b");
    EXPECT_EQ(client_.hlen(k), 3);
    EXPECT_TRUE(client_.hexists(k, "f2"));
    EXPECT_EQ(client_.hstrlen(k, "f2"), 2);

    auto vals = client_.hmget(k, {"f1", "missing"});
    ASSERT_EQ(vals.size(), 2u);
    EXPECT_EQ(*vals[0], "v1b");
    EXPECT_FALSE(vals[1].has_value());

    auto all = client_.hgetall(k);
    EXPECT_EQ(all.size(), 3u);

    EXPECT_EQ(client_.hdel(k, {"f3"}), 1);
    EXPECT_EQ(client_.hlen(k), 2);
}

TEST_F(IntegrationTest, HashIncrAndSetnx) {
    std::string k = key("hashincr");
    EXPECT_EQ(client_.hincrby(k, "n", 5), 5);
    EXPECT_DOUBLE_EQ(client_.hincrbyfloat(k, "f", 1.5), 1.5);
    EXPECT_TRUE(client_.hsetnx(k, "new", "v"));
    EXPECT_FALSE(client_.hsetnx(k, "new", "v2"));
}

TEST_F(IntegrationTest, HashKeysValsRandfield) {
    std::string k = key("hashkv");
    client_.hset(k, {{"a", "1"}, {"b", "2"}});
    auto keys = client_.hkeys(k);
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(client_.hvals(k).size(), 2u);
    EXPECT_EQ(client_.hrandfield(k, 2).size(), 2u);
    EXPECT_EQ(client_.hrandfield_withvalues(k, 2).size(), 2u);
}

// --- sorted sets ---

TEST_F(IntegrationTest, ZaddZscoreZrank) {
    std::string k = key("zset");
    EXPECT_EQ(client_.zadd(k, {{"a", 1}, {"b", 2}, {"c", 3}}), 3);
    EXPECT_DOUBLE_EQ(*client_.zscore(k, "b"), 2.0);
    EXPECT_EQ(client_.zrank(k, "a"), 0);
    EXPECT_EQ(client_.zrevrank(k, "a"), 2);
    EXPECT_EQ(client_.zcard(k), 3);
    EXPECT_EQ(client_.zcount(k, "1", "2"), 2);
    EXPECT_DOUBLE_EQ(client_.zincrby(k, 10, "a"), 11.0);
}

TEST_F(IntegrationTest, ZaddFlags) {
    std::string k = key("zaddflags");
    hotkv::ZAddOptions nx;
    nx.nx = true;
    client_.zadd(k, {{"a", 1}}, nx);
    client_.zadd(k, {{"a", 99}}, nx); // NX: no-op, already exists
    EXPECT_DOUBLE_EQ(*client_.zscore(k, "a"), 1.0);

    auto incr_result = client_.zadd_incr(k, 5, "a");
    ASSERT_TRUE(incr_result.has_value());
    EXPECT_DOUBLE_EQ(*incr_result, 6.0);
}

TEST_F(IntegrationTest, ZrangeIndexScoreLexAndRev) {
    std::string k = key("zrange");
    client_.zadd(k, {{"a", 1}, {"b", 2}, {"c", 3}});

    EXPECT_EQ(client_.zrange(k, "0", "-1"), (std::vector<std::string>{"a", "b", "c"}));

    ZRangeOptions rev;
    rev.rev = true;
    EXPECT_EQ(client_.zrange(k, "0", "-1", rev), (std::vector<std::string>{"c", "b", "a"}));

    ZRangeOptions by_score;
    by_score.by = ZRangeBy::Score;
    EXPECT_EQ(client_.zrange(k, "2", "3", by_score), (std::vector<std::string>{"b", "c"}));

    auto with_scores = client_.zrange_withscores(k, "0", "-1");
    ASSERT_EQ(with_scores.size(), 3u);
    EXPECT_DOUBLE_EQ(with_scores[1].score, 2.0);

    std::string dst = key("zrangestoredst");
    EXPECT_EQ(client_.zrangestore(dst, k, "0", "-1"), 3);
}

TEST_F(IntegrationTest, ZrangestoreByscoreAndBylex) {
    std::string k = key("zrangestorebyscore");
    client_.zadd(k, {{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}});

    std::string dst = key("zrangestorebyscoredst");
    ZRangeOptions by_score;
    by_score.by = ZRangeBy::Score;
    EXPECT_EQ(client_.zrangestore(dst, k, "2", "3", by_score), 2);
    auto stored = client_.zrange(dst, "0", "-1");
    std::sort(stored.begin(), stored.end());
    EXPECT_EQ(stored, (std::vector<std::string>{"b", "c"}));

    ZRangeOptions by_score_rev_limit;
    by_score_rev_limit.by = ZRangeBy::Score;
    by_score_rev_limit.rev = true;
    by_score_rev_limit.limit_offset = 0;
    by_score_rev_limit.limit_count = 2;
    EXPECT_EQ(client_.zrangestore(dst, k, "4", "1", by_score_rev_limit), 2);
    EXPECT_EQ(client_.zrange(dst, "0", "-1"), (std::vector<std::string>{"c", "d"}));

    std::string lex = key("zrangestorebylex");
    client_.zadd(lex, {{"a", 0}, {"b", 0}, {"c", 0}});
    ZRangeOptions by_lex;
    by_lex.by = ZRangeBy::Lex;
    EXPECT_EQ(client_.zrangestore(dst, lex, "-", "+", by_lex), 3);
    EXPECT_EQ(client_.zrange(dst, "0", "-1"), (std::vector<std::string>{"a", "b", "c"}));
}

TEST_F(IntegrationTest, ZrangebyscoreLegacyForms) {
    std::string k = key("zrbs");
    client_.zadd(k, {{"a", 1}, {"b", 2}, {"c", 3}});
    EXPECT_EQ(client_.zrangebyscore(k, "1", "2"), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(client_.zrevrangebyscore(k, "3", "2"), (std::vector<std::string>{"c", "b"}));
    EXPECT_EQ(client_.zrangebylex(k, "-", "+"), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(client_.zrevrangebylex(k, "+", "-"), (std::vector<std::string>{"c", "b", "a"}));
}

TEST_F(IntegrationTest, ZremRangeFamily) {
    std::string k = key("zremrange");
    client_.zadd(k, {{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}});
    EXPECT_EQ(client_.zremrangebyrank(k, 0, 0), 1);
    EXPECT_EQ(client_.zremrangebyscore(k, "4", "4"), 1);
    EXPECT_EQ(client_.zrange(k, "0", "-1"), (std::vector<std::string>{"b", "c"}));
    EXPECT_EQ(client_.zremrangebylex(k, "[b", "[b"), 1);
}

TEST_F(IntegrationTest, ZpopMinMax) {
    std::string k = key("zpop");
    client_.zadd(k, {{"a", 1}, {"b", 2}, {"c", 3}});
    auto low = client_.zpopmin(k);
    ASSERT_EQ(low.size(), 1u);
    EXPECT_EQ(low[0].member, "a");
    auto high = client_.zpopmax(k, 2);
    EXPECT_EQ(high.size(), 2u);
}

TEST_F(IntegrationTest, ZrandmemberFamily) {
    std::string k = key("zrand");
    client_.zadd(k, {{"a", 1}, {"b", 2}});
    EXPECT_EQ(client_.zrandmember(k, 2).size(), 2u);
    EXPECT_EQ(client_.zrandmember_withscores(k, 2).size(), 2u);
}

TEST_F(IntegrationTest, ZsetStoreAlgebraWithWeightsAndAggregate) {
    std::string a = key("zseta");
    std::string b = key("zsetb");
    std::string dst = key("zsetdst");
    client_.zadd(a, {{"x", 1}, {"y", 2}});
    client_.zadd(b, {{"x", 10}, {"z", 3}});

    hotkv::ZStoreOptions opts;
    opts.weights = {1, 2};
    opts.aggregate = "MAX";
    EXPECT_EQ(client_.zunionstore(dst, {a, b}, opts), 3);
    EXPECT_DOUBLE_EQ(*client_.zscore(dst, "x"), 20.0); // max(1*1, 10*2)

    EXPECT_EQ(client_.zinterstore(dst, {a, b}), 1);
    EXPECT_EQ(client_.zdiffstore(dst, {a, b}), 1);

    EXPECT_EQ(client_.zunion({a, b}).size(), 3u);
    EXPECT_EQ(client_.zunion_withscores({a, b}).size(), 3u);
    EXPECT_EQ(client_.zinter({a, b}), (std::vector<std::string>{"x"}));
    EXPECT_EQ(client_.zinter_withscores({a, b}).size(), 1u);
    EXPECT_EQ(client_.zintercard({a, b}), 1);
    EXPECT_EQ(client_.zdiff({a, b}), (std::vector<std::string>{"y"}));
    EXPECT_EQ(client_.zdiff_withscores({a, b}).size(), 1u);
}

TEST_F(IntegrationTest, ZmscoreMultipleMembers) {
    std::string k = key("zmscore");
    client_.zadd(k, {{"a", 1}});
    auto scores = client_.zmscore(k, {"a", "missing"});
    ASSERT_EQ(scores.size(), 2u);
    EXPECT_DOUBLE_EQ(*scores[0], 1.0);
    EXPECT_FALSE(scores[1].has_value());
}
