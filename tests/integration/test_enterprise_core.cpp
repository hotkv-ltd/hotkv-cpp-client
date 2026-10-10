// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <algorithm>

using hotkv::testutil::IntegrationTest;

// TAG.*, HOTKEYS, GCRA.* and VSIM.* are all core (no license feature
// gates them), except VSIM.SAVE/LOAD/LIST/DROP persistence.

TEST_F(IntegrationTest, TagSetRemGetMembersInterCountExists) {
    std::string k = key("tagged");
    client_.set(k, "v"); // TAG.SET requires the key to already exist

    EXPECT_EQ(client_.tag().set(k, {"red", "big"}), 2);
    EXPECT_EQ(client_.tag().set(k, {"red"}), 0); // already present

    auto tags = client_.tag().get(k);
    std::sort(tags.begin(), tags.end());
    EXPECT_EQ(tags, (std::vector<std::string>{"big", "red"}));
    EXPECT_EQ(client_.tag().count(k), 2);
    EXPECT_TRUE(client_.tag().exists(k, "red"));
    EXPECT_FALSE(client_.tag().exists(k, "green"));

    auto members = client_.tag().members("red");
    EXPECT_NE(std::find(members.begin(), members.end(), k), members.end());

    auto inter = client_.tag().inter({"red", "big"});
    EXPECT_NE(std::find(inter.begin(), inter.end(), k), inter.end());

    EXPECT_EQ(client_.tag().rem(k, {"big"}), 1);
    EXPECT_EQ(client_.tag().count(k), 1);
}

TEST_F(IntegrationTest, HotKeysTopAndReset) {
    std::string k = key("hot");
    client_.set(k, "v");
    client_.get(k);
    client_.get(k);

    auto top = client_.hotkeys().top(5);
    // Non-empty on a server that has served any GET at all; do not
    // assert this exact key is present since HOTKEYS is a sampled,
    // server-wide structure shared by concurrent test runs.
    EXPECT_NO_THROW(client_.hotkeys().reset());
    (void)top;
}

TEST_F(IntegrationTest, GcraLimitAndReset) {
    std::string k = key("gcra");
    auto first = client_.gcra().limit(k, 1000, 2);
    EXPECT_FALSE(first.denied);
    EXPECT_EQ(first.retry_after_ms, -1);

    client_.gcra().limit(k, 1000, 2);
    auto third = client_.gcra().limit(k, 1000, 2);
    EXPECT_TRUE(third.denied);
    EXPECT_GE(third.retry_after_ms, 0);

    EXPECT_EQ(client_.gcra().reset({k}), 1);
}

TEST_F(IntegrationTest, VsimIndexLifecycle) {
    std::string index = key("vsimidx");
    client_.vsim().create_index(index, 3);

    EXPECT_EQ(client_.vsim().set(index, "v1", {1.0, 0.0, 0.0}), true);
    EXPECT_EQ(client_.vsim().set(index, "v2", {0.0, 1.0, 0.0}), true);
    EXPECT_EQ(client_.vsim().set(index, "v1", {1.0, 0.0, 0.0}), false); // replaced, not new

    auto got = client_.vsim().get(index, "v1");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->size(), 3u);
    EXPECT_DOUBLE_EQ((*got)[0], 1.0);

    auto results = client_.vsim().search(index, 2, {1.0, 0.0, 0.0});
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].key, "v1");

    auto names = client_.vsim().list_indexes();
    EXPECT_NE(std::find(names.begin(), names.end(), index), names.end());

    auto info = client_.vsim().info(index);
    EXPECT_EQ(info.name, index);
    EXPECT_EQ(info.dimensions, 3);
    EXPECT_EQ(info.vectors, 2);

    EXPECT_TRUE(client_.vsim().del(index, "v2"));
    client_.vsim().drop_index(index);
}

TEST_F(IntegrationTest, VsimSearchWithTagFilter) {
    std::string index = key("vsimfilter");
    client_.vsim().create_index(index, 2);
    client_.vsim().set(index, "a", {1.0, 0.0});
    client_.vsim().set(index, "b", {0.9, 0.1});
    client_.set(index + ":a", "x"); // TAG.SET requires an existing key
    client_.tag().set(index + ":a", {"red"});

    // FILTER restricts to keys carrying the tag via TAG.INTER, so this
    // just needs to not error with a filter tag that matches nothing in
    // this index's own key namespace.
    EXPECT_NO_THROW(client_.vsim().search(index, 5, {1.0, 0.0}, {"nonexistent-tag"}));
    client_.vsim().drop_index(index);
}

TEST_F(IntegrationTest, VsimPersistenceSaveLoadListDrop) {
    // Snapshot names are file names on the server: no '.', ':' or path
    // separators, so the usual colon-separated test key cannot be used.
    std::string snapshot = "vsimsnap" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "x";
    try {
        int64_t written = client_.vsim().save(snapshot, "blob-contents");
        EXPECT_GT(written, 0);
        auto loaded = client_.vsim().load(snapshot);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(*loaded, "blob-contents");
        auto names = client_.vsim().list_snapshots();
        EXPECT_NE(std::find(names.begin(), names.end(), snapshot), names.end());
        EXPECT_TRUE(client_.vsim().drop_snapshot(snapshot));
    } catch (const hotkv::NotLicensedError&) {
        GTEST_SKIP() << "server license does not include 'vsim-persist'";
    }
}
