// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/reply.hpp>

#include <hotkv/error.hpp>

#include <gtest/gtest.h>

using namespace hotkv;

TEST(Reply, NilIsNilAndOptionalsAreEmpty) {
    Reply r = Reply::make_null();
    EXPECT_TRUE(r.is_nil());
    EXPECT_EQ(r.as_optional_string(), std::nullopt);
    EXPECT_EQ(r.as_optional_int(), std::nullopt);
    EXPECT_EQ(r.as_optional_double(), std::nullopt);
}

TEST(Reply, BulkStringOptionalHasValue) {
    Reply r = Reply::make_bulk_string("hi");
    ASSERT_TRUE(r.as_optional_string().has_value());
    EXPECT_EQ(*r.as_optional_string(), "hi");
}

TEST(Reply, WrongTypeAccessThrowsReplyTypeError) {
    Reply r = Reply::make_integer(5);
    EXPECT_THROW(r.as_array(), ReplyTypeError);
    EXPECT_THROW(Reply::make_bulk_string("x").as_array(), ReplyTypeError);
}

TEST(Reply, IntegerCoercesFromNumericString) {
    Reply r = Reply::make_bulk_string("42");
    EXPECT_EQ(r.as_int(), 42);
}

TEST(Reply, DoubleCoercesFromNumericString) {
    Reply r = Reply::make_bulk_string("3.5");
    EXPECT_DOUBLE_EQ(r.as_double(), 3.5);
}

TEST(Reply, ErrorAccessors) {
    Reply r = Reply::make_error("WRONGTYPE Operation against a key holding the wrong kind of value");
    EXPECT_TRUE(r.is_error());
    EXPECT_EQ(r.error_code(), "WRONGTYPE");
    EXPECT_THROW(Reply::make_integer(1).error_code(), ReplyTypeError);
}

TEST(Reply, RaiseIfErrorNoopOnNonError) {
    Reply r = Reply::make_simple_string("OK");
    EXPECT_NO_THROW(r.raise_if_error());
}

TEST(Reply, RaiseIfErrorThrowsServerError) {
    Reply r = Reply::make_error("ERR something went wrong");
    EXPECT_THROW(r.raise_if_error(), ServerError);
}

TEST(Reply, RaiseIfErrorThrowsNotLicensedError) {
    Reply r = Reply::make_error("NOLICENSE enterprise feature 'prompt-cache' requires a valid license");
    try {
        r.raise_if_error();
        FAIL() << "expected NotLicensedError";
    } catch (const NotLicensedError& e) {
        EXPECT_EQ(e.code(), "NOLICENSE");
    }
}

TEST(Reply, RaiseIfErrorThrowsMovedErrorWithSlotHostPort) {
    Reply r = Reply::make_error("MOVED 3999 127.0.0.1:6400");
    try {
        r.raise_if_error();
        FAIL() << "expected MovedError";
    } catch (const MovedError& e) {
        EXPECT_EQ(e.slot(), 3999);
        EXPECT_EQ(e.host(), "127.0.0.1");
        EXPECT_EQ(e.port(), 6400);
    }
}

TEST(Reply, RaiseIfErrorThrowsAskError) {
    Reply r = Reply::make_error("ASK 3999 127.0.0.1:6401");
    EXPECT_THROW(r.raise_if_error(), AskError);
}

TEST(Reply, AsStringArraySkipsNilAsEmptyString) {
    Reply::Array arr;
    arr.push_back(Reply::make_bulk_string("a"));
    arr.push_back(Reply::make_null());
    arr.push_back(Reply::make_bulk_string("c"));
    Reply r = Reply::make_array(std::move(arr));
    EXPECT_EQ(r.as_string_array(), (std::vector<std::string>{"a", "", "c"}));
}

TEST(Reply, AsPairsFromFlatArray) {
    Reply::Array arr;
    arr.push_back(Reply::make_bulk_string("entries"));
    arr.push_back(Reply::make_integer(5));
    arr.push_back(Reply::make_bulk_string("hits"));
    arr.push_back(Reply::make_integer(2));
    Reply r = Reply::make_array(std::move(arr));

    auto pairs = r.as_pairs();
    ASSERT_EQ(pairs.size(), 2u);
    EXPECT_EQ(pairs[0].first, "entries");
    EXPECT_EQ(pairs[0].second.as_int(), 5);
    EXPECT_EQ(pairs[1].first, "hits");
    EXPECT_EQ(pairs[1].second.as_int(), 2);
}

TEST(Reply, AsPairsFromMap) {
    Reply::MapEntries entries;
    entries.emplace_back(Reply::make_bulk_string("entries"), Reply::make_integer(5));
    Reply r = Reply::make_map(std::move(entries));

    auto pairs = r.as_pairs();
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0].first, "entries");
    EXPECT_EQ(pairs[0].second.as_int(), 5);
}

TEST(Reply, IsArrayLikeForArraySetPush) {
    EXPECT_TRUE(Reply::make_array({}).is_array_like());
    EXPECT_TRUE(Reply::make_set({}).is_array_like());
    EXPECT_TRUE(Reply::make_push({}).is_array_like());
    EXPECT_FALSE(Reply::make_map({}).is_array_like());
    EXPECT_FALSE(Reply::make_integer(1).is_array_like());
}
