// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

using hotkv::ExpireCondition;
using hotkv::testutil::IntegrationTest;

// --- strings ---

TEST_F(IntegrationTest, GetSetRoundTrip) {
    std::string k = key("str");
    EXPECT_FALSE(client_.get(k).has_value());
    EXPECT_TRUE(client_.set(k, "hello"));
    ASSERT_TRUE(client_.get(k).has_value());
    EXPECT_EQ(*client_.get(k), "hello");
}

TEST_F(IntegrationTest, SetNxAndXx) {
    std::string k = key("nxxx");
    hotkv::SetOptions nx;
    nx.nx = true;
    EXPECT_TRUE(client_.set(k, "first", nx));
    EXPECT_FALSE(client_.set(k, "second", nx)); // already exists, NX blocks it
    EXPECT_EQ(*client_.get(k), "first");

    hotkv::SetOptions xx;
    xx.xx = true;
    EXPECT_TRUE(client_.set(k, "third", xx));
    EXPECT_EQ(*client_.get(k), "third");
}

TEST_F(IntegrationTest, SetGetOption) {
    std::string k = key("setget");
    client_.set(k, "old");
    auto previous = client_.set_get(k, "new");
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(*previous, "old");
    EXPECT_EQ(*client_.get(k), "new");
}

TEST_F(IntegrationTest, GetSetClassic) {
    std::string k = key("getset");
    client_.set(k, "a");
    EXPECT_EQ(*client_.getset(k, "b"), "a");
    EXPECT_EQ(*client_.get(k), "b");
}

TEST_F(IntegrationTest, GetDel) {
    std::string k = key("getdel");
    client_.set(k, "gone-soon");
    EXPECT_EQ(*client_.getdel(k), "gone-soon");
    EXPECT_FALSE(client_.get(k).has_value());
}

TEST_F(IntegrationTest, AppendAndStrlen) {
    std::string k = key("append");
    EXPECT_EQ(client_.append(k, "foo"), 3);
    EXPECT_EQ(client_.append(k, "bar"), 6);
    EXPECT_EQ(client_.strlen(k), 6);
    EXPECT_EQ(*client_.get(k), "foobar");
}

TEST_F(IntegrationTest, IncrDecrFamily) {
    std::string k = key("counter");
    EXPECT_EQ(client_.incr(k), 1);
    EXPECT_EQ(client_.incrby(k, 9), 10);
    EXPECT_EQ(client_.decr(k), 9);
    EXPECT_EQ(client_.decrby(k, 4), 5);
    EXPECT_DOUBLE_EQ(client_.incrbyfloat(k, 0.5), 5.5);
}

TEST_F(IntegrationTest, MgetMsetMsetnx) {
    std::string a = key("a");
    std::string b = key("b");
    std::string c = key("c");
    client_.mset({{a, "1"}, {b, "2"}});
    auto values = client_.mget({a, b, c});
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(*values[0], "1");
    EXPECT_EQ(*values[1], "2");
    EXPECT_FALSE(values[2].has_value());

    EXPECT_FALSE(client_.msetnx({{a, "x"}, {c, "3"}})); // a exists, whole op fails
    EXPECT_FALSE(client_.get(c).has_value());
    EXPECT_TRUE(client_.msetnx({{c, "3"}}));
    EXPECT_EQ(*client_.get(c), "3");
}

TEST_F(IntegrationTest, GetRangeSetRange) {
    std::string k = key("range");
    client_.set(k, "Hello World");
    EXPECT_EQ(client_.getrange(k, 0, 4), "Hello");
    EXPECT_EQ(client_.setrange(k, 6, "Redis"), 11);
    EXPECT_EQ(*client_.get(k), "Hello Redis");
}

// --- generic / keys ---

TEST_F(IntegrationTest, ExistsDelUnlink) {
    std::string a = key("a");
    std::string b = key("b");
    client_.set(a, "1");
    client_.set(b, "2");
    EXPECT_EQ(client_.exists({a, b, key("missing")}), 2);
    EXPECT_EQ(client_.del({a}), 1);
    EXPECT_EQ(client_.unlink({b}), 1);
    EXPECT_EQ(client_.exists({a, b}), 0);
}

TEST_F(IntegrationTest, TypeCommand) {
    std::string k = key("type");
    client_.set(k, "x");
    EXPECT_EQ(client_.type(k), "string");
}

TEST_F(IntegrationTest, ExpireTtlPersist) {
    std::string k = key("ttl");
    client_.set(k, "x");
    EXPECT_EQ(client_.ttl(k), -1);
    EXPECT_TRUE(client_.expire(k, 100));
    EXPECT_GT(client_.ttl(k), 0);
    EXPECT_TRUE(client_.persist(k));
    EXPECT_EQ(client_.ttl(k), -1);
    EXPECT_EQ(client_.ttl(key("nosuchkey")), -2);
}

TEST_F(IntegrationTest, ExpireConditions) {
    std::string k = key("cond");
    client_.set(k, "x");
    EXPECT_TRUE(client_.expire(k, 100));
    // NX: fails because a TTL is already set.
    EXPECT_FALSE(client_.expire(k, 200, ExpireCondition::Nx));
    // GT: succeeds only if the new TTL is greater.
    EXPECT_TRUE(client_.expire(k, 500, ExpireCondition::Gt));
    EXPECT_FALSE(client_.expire(k, 10, ExpireCondition::Gt));
}

TEST_F(IntegrationTest, PexpireatAndPttl) {
    std::string k = key("pexpireat");
    client_.set(k, "x");
    int64_t future_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count() +
        60000;
    EXPECT_TRUE(client_.pexpireat(k, future_ms));
    EXPECT_GT(client_.pttl(k), 0);
}

TEST_F(IntegrationTest, KeysAndRandomkey) {
    std::string k = key("keysglob");
    client_.set(k, "x");
    auto found = client_.keys(prefix_ + ":keysglob");
    EXPECT_EQ(found.size(), 1u);
    EXPECT_TRUE(client_.randomkey().has_value());
}

TEST_F(IntegrationTest, RenameAndRenamenx) {
    std::string a = key("rensrc");
    std::string b = key("rendst");
    client_.set(a, "v");
    EXPECT_TRUE(client_.rename(a, b));
    EXPECT_EQ(*client_.get(b), "v");

    std::string c = key("renexists");
    client_.set(c, "other");
    EXPECT_FALSE(client_.renamenx(b, c));
}

TEST_F(IntegrationTest, TouchCommand) {
    std::string k = key("touch");
    client_.set(k, "x");
    EXPECT_EQ(client_.touch({k, key("missing")}), 1);
}

TEST_F(IntegrationTest, DumpAndRestore) {
    std::string src = key("dumpsrc");
    std::string dst = key("dumpdst");
    client_.set(src, "dumpvalue");
    auto serialized = client_.dump(src);
    ASSERT_TRUE(serialized.has_value());
    client_.restore(dst, 0, *serialized);
    EXPECT_EQ(*client_.get(dst), "dumpvalue");
}

TEST_F(IntegrationTest, CopyCommand) {
    std::string src = key("copysrc");
    std::string dst = key("copydst");
    client_.set(src, "v");
    EXPECT_TRUE(client_.copy(src, dst));
    EXPECT_EQ(*client_.get(dst), "v");
    EXPECT_FALSE(client_.copy(src, dst)); // dst already exists, no REPLACE
    EXPECT_TRUE(client_.copy(src, dst, std::nullopt, true));
}

TEST_F(IntegrationTest, ObjectEncoding) {
    std::string k = key("objenc");
    client_.set(k, "x");
    EXPECT_TRUE(client_.object_encoding(k).has_value());
}

// --- nanosecond TTL (HotKV core extension) ---

TEST_F(IntegrationTest, NanosecondTtlFamily) {
    std::string k = key("nttl");
    client_.set(k, "x");
    EXPECT_EQ(client_.nttl(k), -1);
    EXPECT_TRUE(client_.nexpire(k, 60'000'000'000LL)); // 60s in ns
    EXPECT_GT(client_.nttl(k), 0);
    EXPECT_EQ(client_.nttl(key("nosuchkey")), -2);

    int64_t future_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count() +
        60'000'000'000LL;
    EXPECT_TRUE(client_.nexpireat(k, future_ns));
    EXPECT_GT(client_.nttl(k), 0);
}

// --- hash field TTL ---

TEST_F(IntegrationTest, HashFieldTtlFamily) {
    std::string k = key("hfttl");
    client_.hset(k, "f1", "v1");
    client_.hset(k, "f2", "v2");

    auto set_result = client_.hexpire(k, 100, {"f1"});
    ASSERT_EQ(set_result.size(), 1u);
    EXPECT_EQ(set_result[0], 1);

    auto ttls = client_.httl(k, {"f1", "f2"});
    ASSERT_EQ(ttls.size(), 2u);
    EXPECT_GT(ttls[0], 0);
    EXPECT_EQ(ttls[1], -1); // no TTL set on f2

    auto persisted = client_.hpersist(k, {"f1"});
    ASSERT_EQ(persisted.size(), 1u);
    EXPECT_EQ(persisted[0], 1);

    auto pttls = client_.hpttl(k, {"f1"});
    EXPECT_EQ(pttls[0], -1);
}
