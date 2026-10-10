// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <algorithm>

using hotkv::Client;
using hotkv::Options;
using hotkv::Reply;
using hotkv::ScoredMember;
using hotkv::testutil::unique_prefix;

namespace {

// Checks that HGETALL, HRANDFIELD WITHVALUES, SMEMBERS, ZSCORE, ZRANGE
// WITHSCORES, ZPOPMIN with a count, ZRANDMEMBER WITHSCORES, XREAD, XINFO
// STREAM, CONFIG GET and INFO decode to the same shape whether the
// connection negotiated RESP3 (the default) or RESP2.
void run_checks(Client& client, const std::string& label) {
    std::string prefix = unique_prefix("conform-" + label);
    std::string hkey = prefix + ":hash";
    std::string skey = prefix + ":set";
    std::string zkey = prefix + ":zset";
    std::string xkey = prefix + ":stream";

    client.hset(hkey, {{"f1", "v1"}, {"f2", "v2"}});
    auto hgetall = client.hgetall(hkey);
    std::sort(hgetall.begin(), hgetall.end());
    EXPECT_EQ(hgetall, (std::vector<std::pair<std::string, std::string>>{{"f1", "v1"}, {"f2", "v2"}}));

    auto fvs = client.hrandfield_withvalues(hkey, 2);
    ASSERT_EQ(fvs.size(), 2u);
    for (auto& [field, value] : fvs) {
        EXPECT_EQ(value, field == "f1" ? "v1" : "v2");
    }

    client.sadd(skey, {"a", "b", "c"});
    auto members = client.smembers(skey);
    std::sort(members.begin(), members.end());
    EXPECT_EQ(members, (std::vector<std::string>{"a", "b", "c"}));

    client.zadd(zkey, {{"a", 1.0}, {"b", 2.0}, {"c", 3.0}});
    auto score = client.zscore(zkey, "b");
    ASSERT_TRUE(score.has_value());
    EXPECT_DOUBLE_EQ(*score, 2.0);

    auto with_scores = client.zrange_withscores(zkey, "0", "-1");
    ASSERT_EQ(with_scores.size(), 3u);
    EXPECT_EQ(with_scores[0].member, "a");
    EXPECT_DOUBLE_EQ(with_scores[0].score, 1.0);
    EXPECT_EQ(with_scores[2].member, "c");
    EXPECT_DOUBLE_EQ(with_scores[2].score, 3.0);

    auto popped = client.zpopmin(zkey, 2);
    ASSERT_EQ(popped.size(), 2u);
    EXPECT_EQ(popped[0].member, "a");
    EXPECT_EQ(popped[1].member, "b");

    client.zadd(zkey, {{"a", 1.0}, {"b", 2.0}});
    auto rand_scored = client.zrandmember_withscores(zkey, 3);
    EXPECT_EQ(rand_scored.size(), 3u);

    std::string id1 = client.xadd(xkey, {{"field1", "v1"}});
    std::string id2 = client.xadd(xkey, {{"field2", "v2"}});
    auto streams = client.xread({{xkey, "0"}});
    ASSERT_EQ(streams.size(), 1u);
    EXPECT_EQ(streams[0].stream, xkey);
    ASSERT_EQ(streams[0].entries.size(), 2u);
    EXPECT_EQ(streams[0].entries[0].id, id1);
    EXPECT_EQ(streams[0].entries[0].fields, (std::vector<std::pair<std::string, std::string>>{{"field1", "v1"}}));
    EXPECT_EQ(streams[0].entries[1].id, id2);

    Reply info = client.xinfo_stream(xkey);
    bool found_length = false;
    for (auto& [name, value] : info.as_pairs()) {
        if (name == "length") {
            EXPECT_EQ(value.as_int(), 2);
            found_length = true;
        }
    }
    EXPECT_TRUE(found_length);

    auto cfg = client.config_get("maxmemory");
    bool found_maxmemory = std::any_of(cfg.begin(), cfg.end(), [](auto& kv) { return kv.first == "maxmemory"; });
    EXPECT_TRUE(found_maxmemory);

    std::string info_str = client.info();
    EXPECT_FALSE(info_str.rfind("txt:", 0) == 0) << "INFO leaked a verbatim string encoding prefix";
    EXPECT_FALSE(info_str.rfind("mkd:", 0) == 0) << "INFO leaked a verbatim string encoding prefix";
    EXPECT_NE(info_str.find("hotkv_version"), std::string::npos);

    client.del({hkey, skey, zkey, xkey});
}

} // namespace

TEST(RespConformance, Resp3) {
    HOTKV_SKIP_IF_NO_SERVER(url);
    Options opts = Options::parse_url(url);
    opts.use_resp3 = true;
    Client client = Client::connect(opts);
    run_checks(client, "resp3");
}

TEST(RespConformance, Resp2) {
    HOTKV_SKIP_IF_NO_SERVER(url);
    Options opts = Options::parse_url(url);
    opts.use_resp3 = false;
    Client client = Client::connect(opts);
    run_checks(client, "resp2");
}
