// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/scan.hpp>

#include <hotkv/error.hpp>

#include <gtest/gtest.h>

using namespace hotkv;

namespace {

Reply page_reply(const std::string& cursor, std::vector<std::string> items) {
    Reply::Array items_arr;
    for (auto& s : items) {
        items_arr.push_back(Reply::make_bulk_string(std::move(s)));
    }
    Reply::Array top;
    top.push_back(Reply::make_bulk_string(cursor));
    top.push_back(Reply::make_array(std::move(items_arr)));
    return Reply::make_array(std::move(top));
}

} // namespace

TEST(Scanner, SingleStrideAcrossTwoPages) {
    int calls = 0;
    Scanner scanner(
        [&](const std::string& cursor) {
            ++calls;
            if (cursor == "0") {
                return page_reply("5", {"a", "b"});
            }
            EXPECT_EQ(cursor, "5");
            return page_reply("0", {"c"});
        },
        1);

    std::vector<std::string> collected;
    std::string value;
    while (scanner.next(value)) {
        collected.push_back(value);
    }
    EXPECT_EQ(collected, (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(calls, 2);
}

TEST(Scanner, EmptyPageInTheMiddleStillContinues) {
    int step = 0;
    Scanner scanner(
        [&](const std::string&) {
            ++step;
            if (step == 1) {
                return page_reply("1", {});
            }
            if (step == 2) {
                return page_reply("0", {"only"});
            }
            ADD_FAILURE() << "should not be called a third time";
            return page_reply("0", {});
        },
        1);

    std::string value;
    ASSERT_TRUE(scanner.next(value));
    EXPECT_EQ(value, "only");
    EXPECT_FALSE(scanner.next(value));
}

TEST(Scanner, ImmediateCompletionYieldsNothing) {
    Scanner scanner([&](const std::string&) { return page_reply("0", {}); }, 1);
    std::string value;
    EXPECT_FALSE(scanner.next(value));
}

TEST(Scanner, PairStrideYieldsFieldValuePairs) {
    Scanner scanner(
        [&](const std::string&) { return page_reply("0", {"field1", "value1", "field2", "value2"}); }, 2);

    std::string a, b;
    ASSERT_TRUE(scanner.next(a, b));
    EXPECT_EQ(a, "field1");
    EXPECT_EQ(b, "value1");
    ASSERT_TRUE(scanner.next(a, b));
    EXPECT_EQ(a, "field2");
    EXPECT_EQ(b, "value2");
    EXPECT_FALSE(scanner.next(a, b));
}

TEST(Scanner, NextScoredParsesDoubleScore) {
    Scanner scanner([&](const std::string&) { return page_reply("0", {"member1", "1.5"}); }, 2);
    std::string member;
    double score = 0.0;
    ASSERT_TRUE(scanner.next_scored(member, score));
    EXPECT_EQ(member, "member1");
    EXPECT_DOUBLE_EQ(score, 1.5);
}

TEST(Scanner, WrongArityCallThrows) {
    Scanner single_stride([&](const std::string&) { return page_reply("0", {"a"}); }, 1);
    std::string a, b;
    EXPECT_THROW(single_stride.next(a, b), Error);

    Scanner pair_stride([&](const std::string&) { return page_reply("0", {"a", "b"}); }, 2);
    std::string value;
    EXPECT_THROW(pair_stride.next(value), Error);
}
