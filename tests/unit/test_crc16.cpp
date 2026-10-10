// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/cluster.hpp>

#include <gtest/gtest.h>

// Test vectors verified against CLUSTER
// KEYSLOT / the canonical CRC16/XMODOM check value.

TEST(Crc16, XmodemCheckValue) {
    EXPECT_EQ(hotkv::crc16("123456789"), 0x31C3);
}

TEST(KeySlot, PlainKeysNoHashTag) {
    EXPECT_EQ(hotkv::key_slot("foo"), 12182);
    EXPECT_EQ(hotkv::key_slot("bar"), 5061);
    EXPECT_EQ(hotkv::key_slot("hello"), 866);
}

TEST(KeySlot, HashTagsRouteToSameSlot) {
    EXPECT_EQ(hotkv::key_slot("{user1000}.following"), 3443);
    EXPECT_EQ(hotkv::key_slot("{user1000}.followers"), 3443);
}

TEST(KeySlot, EmptyHashTagHashesWholeKey) {
    EXPECT_EQ(hotkv::key_slot("{}foo"), 9500);
    EXPECT_EQ(hotkv::key_slot("foo{}{bar}"), 8363);
}

TEST(KeySlot, FirstClosingBraceAfterFirstOpeningBraceWins) {
    EXPECT_EQ(hotkv::key_slot("foo{{bar}}zap"), 4015);
}

TEST(KeySlot, HashTagMatchesPlainKeyOfSameSubstring) {
    EXPECT_EQ(hotkv::key_slot("foo{bar}{zap}"), 5061);
    EXPECT_EQ(hotkv::key_slot("foo{bar}{zap}"), hotkv::key_slot("bar"));
}

TEST(KeySlot, WithinClusterSlotRange) {
    for (const char* key : {"a", "b", "some-longer-key", "{tag}rest", ""}) {
        EXPECT_LT(hotkv::key_slot(key), 16384);
    }
}
