// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

using hotkv::GeoSearchOptions;
using hotkv::GeoUnit;
using hotkv::Reply;
using hotkv::testutil::IntegrationTest;

// --- streams ---

TEST_F(IntegrationTest, StreamAddRangeLen) {
    std::string k = key("stream");
    std::string id1 = client_.xadd(k, {{"field", "value1"}});
    std::string id2 = client_.xadd(k, {{"field", "value2"}});
    EXPECT_NE(id1, id2);
    EXPECT_EQ(client_.xlen(k), 2);

    auto entries = client_.xrange(k, "-", "+");
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].id, id1);
    ASSERT_EQ(entries[0].fields.size(), 1u);
    EXPECT_EQ(entries[0].fields[0].first, "field");
    EXPECT_EQ(entries[0].fields[0].second, "value1");

    auto rev = client_.xrevrange(k, "+", "-", 1);
    ASSERT_EQ(rev.size(), 1u);
    EXPECT_EQ(rev[0].id, id2);

    EXPECT_EQ(client_.xdel(k, {id1}), 1);
    EXPECT_EQ(client_.xlen(k), 1);
}

TEST_F(IntegrationTest, StreamMaxlenTrim) {
    std::string k = key("streamtrim");
    for (int i = 0; i < 5; ++i) {
        client_.xadd_maxlen(k, 3, false, {{"i", std::to_string(i)}});
    }
    ASSERT_EQ(client_.xlen(k), 3); // each XADD ... MAXLEN 3 trims down to at most 3
    EXPECT_EQ(client_.xtrim_maxlen(k, 1), 2); // trims 3 -> 1, so 2 entries removed
    EXPECT_EQ(client_.xlen(k), 1);
}

TEST_F(IntegrationTest, StreamReadNonBlocking) {
    std::string k = key("streamread");
    client_.xadd(k, {{"a", "1"}});
    auto result = client_.xread({{k, "0"}});
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].stream, k);
    EXPECT_EQ(result[0].entries.size(), 1u);
}

TEST_F(IntegrationTest, StreamConsumerGroups) {
    std::string k = key("streamgroup");
    client_.xadd(k, {{"a", "1"}});
    client_.xgroup_create(k, "grp", "0");

    auto read = client_.xreadgroup("grp", "consumer1", {{k, ">"}});
    ASSERT_EQ(read.size(), 1u);
    ASSERT_EQ(read[0].entries.size(), 1u);
    std::string entry_id = read[0].entries[0].id;

    auto pending = client_.xpending(k, "grp");
    EXPECT_EQ(pending.count, 1);

    auto claimed = client_.xclaim(k, "grp", "consumer2", 0, {entry_id});
    EXPECT_EQ(claimed.size(), 1u);

    auto autoclaimed = client_.xautoclaim(k, "grp", "consumer1", 0, "0");
    EXPECT_FALSE(autoclaimed.cursor.empty());

    EXPECT_EQ(client_.xack(k, "grp", {entry_id}), 1);
    EXPECT_TRUE(client_.xgroup_createconsumer(k, "grp", "consumer3"));
    EXPECT_GE(client_.xgroup_delconsumer(k, "grp", "consumer3"), 0);
    client_.xgroup_setid(k, "grp", "0");

    // XINFO STREAM replies as a RESP3 map (field/value pairs); XINFO
    // GROUPS/CONSUMERS as an array of maps. as_pairs() (on the top-level
    // reply, or per element) decodes either without assuming which.
    Reply stream_info = client_.xinfo_stream(k);
    EXPECT_FALSE(stream_info.as_pairs().empty());
    Reply groups_info = client_.xinfo_groups(k);
    ASSERT_FALSE(groups_info.as_array().empty());
    EXPECT_FALSE(groups_info.as_array()[0].as_pairs().empty());
    Reply consumers_info = client_.xinfo_consumers(k, "grp");
    (void)consumers_info;

    EXPECT_TRUE(client_.xgroup_destroy(k, "grp"));
}

// --- geo ---

TEST_F(IntegrationTest, GeoAddDistPosHash) {
    std::string k = key("geo");
    EXPECT_EQ(client_.geoadd(k, {{13.361389, 38.115556, "Palermo"}, {15.087269, 37.502669, "Catania"}}), 2);

    auto dist = client_.geodist(k, "Palermo", "Catania", GeoUnit::Kilometers);
    ASSERT_TRUE(dist.has_value());
    EXPECT_GT(*dist, 100.0);
    EXPECT_LT(*dist, 200.0);

    auto positions = client_.geopos(k, {"Palermo", "missing"});
    ASSERT_EQ(positions.size(), 2u);
    ASSERT_TRUE(positions[0].has_value());
    EXPECT_NEAR(positions[0]->longitude, 13.361389, 0.001);
    EXPECT_FALSE(positions[1].has_value());

    auto hashes = client_.geohash(k, {"Palermo"});
    ASSERT_EQ(hashes.size(), 1u);
    EXPECT_TRUE(hashes[0].has_value());
}

TEST_F(IntegrationTest, GeoSearchByRadius) {
    std::string k = key("geosearch");
    client_.geoadd(k, {{13.361389, 38.115556, "Palermo"}, {15.087269, 37.502669, "Catania"}});

    GeoSearchOptions opts;
    opts.from_member = "Palermo";
    opts.radius = 200;
    opts.unit = GeoUnit::Kilometers;
    opts.with_dist = true;
    auto results = client_.geosearch(k, opts);
    EXPECT_EQ(results.size(), 2u);
    for (auto& r : results) {
        EXPECT_TRUE(r.distance.has_value());
    }

    // GEOSEARCHSTORE rejects the WITHCOORD/WITHDIST/WITHHASH options
    // that GEOSEARCH accepts (as in the standard command), so it needs its own
    // options value rather than reusing one built for a read.
    GeoSearchOptions store_opts;
    store_opts.from_member = "Palermo";
    store_opts.radius = 200;
    store_opts.unit = GeoUnit::Kilometers;
    std::string dst = key("geosearchstore");
    EXPECT_EQ(client_.geosearchstore(dst, k, store_opts), 2);
}

// --- bitmaps ---

TEST_F(IntegrationTest, BitSetGetCount) {
    std::string k = key("bits");
    EXPECT_EQ(client_.setbit(k, 7, 1), 0);
    EXPECT_EQ(client_.getbit(k, 7), 1);
    EXPECT_EQ(client_.bitcount(k), 1);
    EXPECT_EQ(client_.bitcount(k, 0, -1), 1);
    EXPECT_EQ(client_.bitpos(k, 1), 7);
}

TEST_F(IntegrationTest, BitOpAndField) {
    std::string a = key("bitopa");
    std::string b = key("bitopb");
    std::string dst = key("bitopdst");
    client_.set(a, "abc");
    client_.set(b, "abd");
    EXPECT_GT(client_.bitop_and(dst, {a, b}), 0);
    EXPECT_GT(client_.bitop_or(dst, {a, b}), 0);
    EXPECT_GT(client_.bitop_xor(dst, {a, b}), 0);
    EXPECT_GT(client_.bitop_not(dst, a), 0);

    std::string bf = key("bitfield");
    auto results = client_.bitfield(bf, {"SET", "u8", "0", "255", "GET", "u8", "0"});
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(*results[1], 255);
}

// --- HyperLogLog ---

TEST_F(IntegrationTest, HyperLogLogAddMergeCount) {
    std::string a = key("hll_a");
    std::string b = key("hll_b");
    std::string dst = key("hll_dst");
    EXPECT_TRUE(client_.pfadd(a, {"x", "y", "z"}));
    EXPECT_TRUE(client_.pfadd(b, {"z", "w"}));
    client_.pfmerge(dst, {a, b});
    EXPECT_GE(client_.pfcount({dst}), 3);
    EXPECT_GE(client_.pfcount({a, b}), 3);
}
