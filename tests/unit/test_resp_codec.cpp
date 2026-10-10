// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "resp_codec.hpp"

#include "fake_stream.hpp"

#include <hotkv/error.hpp>

#include <gtest/gtest.h>

#include <cmath>

using hotkv::Reply;
using hotkv::ReplyType;
using hotkv::detail::encode_command;
using hotkv::detail::RespReader;
using hotkv::testutil::FakeStream;
using hotkv::testutil::split_into_chunks;

namespace {

Reply decode(const std::string& wire, std::size_t chunk_size = 4096) {
    FakeStream stream(split_into_chunks(wire, chunk_size));
    RespReader reader(stream);
    return reader.read_reply();
}

} // namespace

TEST(EncodeCommand, BuildsArrayOfBulkStrings) {
    std::string wire = encode_command({"SET", "key", "value"});
    EXPECT_EQ(wire, "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n");
}

TEST(EncodeCommand, EmptyArgsProducesZeroLengthArray) {
    EXPECT_EQ(encode_command({}), "*0\r\n");
}

TEST(EncodeCommand, BinarySafeArgument) {
    std::string binary("a\r\nb\0c", 6);
    std::string wire = encode_command({"SET", "k", binary});
    EXPECT_EQ(wire, "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$6\r\n" + binary + "\r\n");
}

TEST(RespDecode, SimpleString) {
    Reply r = decode("+OK\r\n");
    EXPECT_EQ(r.type(), ReplyType::SimpleString);
    EXPECT_EQ(r.as_string(), "OK");
}

TEST(RespDecode, Error) {
    Reply r = decode("-WRONGTYPE Operation against a key\r\n");
    EXPECT_TRUE(r.is_error());
    EXPECT_EQ(r.error_code(), "WRONGTYPE");
}

TEST(RespDecode, Integer) {
    Reply r = decode(":12345\r\n");
    EXPECT_EQ(r.type(), ReplyType::Integer);
    EXPECT_EQ(r.as_int(), 12345);
}

TEST(RespDecode, NegativeInteger) {
    EXPECT_EQ(decode(":-42\r\n").as_int(), -42);
}

TEST(RespDecode, BulkString) {
    Reply r = decode("$5\r\nhello\r\n");
    EXPECT_EQ(r.type(), ReplyType::BulkString);
    EXPECT_EQ(r.as_string(), "hello");
}

TEST(RespDecode, BulkStringBinarySafe) {
    std::string payload("a\r\nb\0c\xff", 7);
    std::string wire = "$7\r\n" + payload + "\r\n";
    Reply r = decode(wire);
    EXPECT_EQ(r.as_string(), payload);
}

TEST(RespDecode, NullBulkString) {
    Reply r = decode("$-1\r\n");
    EXPECT_TRUE(r.is_nil());
}

TEST(RespDecode, EmptyBulkString) {
    Reply r = decode("$0\r\n\r\n");
    EXPECT_EQ(r.as_string(), "");
}

TEST(RespDecode, Array) {
    Reply r = decode("*3\r\n:1\r\n:2\r\n:3\r\n");
    ASSERT_EQ(r.type(), ReplyType::Array);
    const auto& arr = r.as_array();
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr[0].as_int(), 1);
    EXPECT_EQ(arr[2].as_int(), 3);
}

TEST(RespDecode, NullArray) {
    Reply r = decode("*-1\r\n");
    EXPECT_TRUE(r.is_nil());
}

TEST(RespDecode, NestedArray) {
    Reply r = decode("*2\r\n*2\r\n:1\r\n:2\r\n$3\r\nfoo\r\n");
    const auto& outer = r.as_array();
    ASSERT_EQ(outer.size(), 2u);
    const auto& inner = outer[0].as_array();
    EXPECT_EQ(inner[0].as_int(), 1);
    EXPECT_EQ(inner[1].as_int(), 2);
    EXPECT_EQ(outer[1].as_string(), "foo");
}

TEST(RespDecode, EmptyArray) {
    Reply r = decode("*0\r\n");
    EXPECT_TRUE(r.as_array().empty());
}

// --- RESP3 types ---

TEST(RespDecode, Resp3Null) {
    EXPECT_TRUE(decode("_\r\n").is_nil());
}

TEST(RespDecode, Resp3BooleanTrue) {
    Reply r = decode("#t\r\n");
    EXPECT_EQ(r.type(), ReplyType::Boolean);
    EXPECT_TRUE(r.as_bool());
}

TEST(RespDecode, Resp3BooleanFalse) {
    EXPECT_FALSE(decode("#f\r\n").as_bool());
}

TEST(RespDecode, Resp3Double) {
    Reply r = decode(",3.14159\r\n");
    EXPECT_EQ(r.type(), ReplyType::Double);
    EXPECT_DOUBLE_EQ(r.as_double(), 3.14159);
}

TEST(RespDecode, Resp3DoubleInfinity) {
    EXPECT_TRUE(std::isinf(decode(",inf\r\n").as_double()));
    EXPECT_LT(decode(",-inf\r\n").as_double(), 0.0);
}

TEST(RespDecode, Resp3DoubleNan) {
    EXPECT_TRUE(std::isnan(decode(",nan\r\n").as_double()));
}

TEST(RespDecode, Resp3BigNumber) {
    Reply r = decode("(3492890328409238509324850943850943825024385\r\n");
    EXPECT_EQ(r.type(), ReplyType::BigNumber);
    EXPECT_EQ(r.as_string(), "3492890328409238509324850943850943825024385");
}

TEST(RespDecode, Resp3VerbatimString) {
    Reply r = decode("=15\r\ntxt:Some string\r\n");
    EXPECT_EQ(r.type(), ReplyType::Verbatim);
    EXPECT_EQ(r.verbatim_format(), "txt");
    EXPECT_EQ(r.as_string(), "Some string");
}

TEST(RespDecode, Resp3BlobError) {
    Reply r = decode("!21\r\nSYNTAX invalid syntax\r\n");
    EXPECT_TRUE(r.is_error());
    EXPECT_EQ(r.error_code(), "SYNTAX");
}

TEST(RespDecode, Resp3Map) {
    Reply r = decode("%2\r\n$3\r\nfoo\r\n:1\r\n$3\r\nbar\r\n:2\r\n");
    ASSERT_EQ(r.type(), ReplyType::Map);
    const auto& pairs = r.as_pairs();
    ASSERT_EQ(pairs.size(), 2u);
    EXPECT_EQ(pairs[0].first, "foo");
    EXPECT_EQ(pairs[0].second.as_int(), 1);
    EXPECT_EQ(pairs[1].first, "bar");
    EXPECT_EQ(pairs[1].second.as_int(), 2);
}

TEST(RespDecode, Resp3Set) {
    Reply r = decode("~2\r\n$1\r\na\r\n$1\r\nb\r\n");
    EXPECT_EQ(r.type(), ReplyType::Set);
    EXPECT_EQ(r.as_string_array(), (std::vector<std::string>{"a", "b"}));
}

TEST(RespDecode, Resp3Push) {
    Reply r = decode(">3\r\n$7\r\nmessage\r\n$2\r\nch\r\n$5\r\nhello\r\n");
    EXPECT_EQ(r.type(), ReplyType::Push);
    EXPECT_EQ(r.as_array().size(), 3u);
}

TEST(RespDecode, Resp3AttributeIsSkippedAndAttached) {
    // An attribute precedes the reply it decorates; the decoder should
    // return the underlying reply (here, an array) with the attribute
    // recorded, not the attribute map itself.
    Reply r = decode("|1\r\n$14\r\nexpired-fields\r\n*2\r\n$1\r\na\r\n$1\r\nb\r\n*2\r\n$1\r\na\r\n$1\r\nb\r\n");
    ASSERT_EQ(r.type(), ReplyType::Array);
    EXPECT_TRUE(r.attributes().has_value());
    EXPECT_EQ(r.attributes()->size(), 1u);
    EXPECT_EQ(r.as_string_array(), (std::vector<std::string>{"a", "b"}));
}

// --- Reply::as_pairs() decodes both RESP2-flat-array and RESP3-map shapes ---

TEST(RespDecode, AsPairsFromFlatArray) {
    Reply r = decode("*4\r\n$7\r\nentries\r\n:5\r\n$4\r\nhits\r\n:2\r\n");
    auto pairs = r.as_pairs();
    ASSERT_EQ(pairs.size(), 2u);
    EXPECT_EQ(pairs[0].first, "entries");
    EXPECT_EQ(pairs[0].second.as_int(), 5);
    EXPECT_EQ(pairs[1].first, "hits");
    EXPECT_EQ(pairs[1].second.as_int(), 2);
}

TEST(RespDecode, AsPairsFromMap) {
    Reply r = decode("%1\r\n$7\r\nentries\r\n:5\r\n");
    auto pairs = r.as_pairs();
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0].first, "entries");
    EXPECT_EQ(pairs[0].second.as_int(), 5);
}

// --- Streaming: the same reply split across many tiny reads ---

TEST(RespDecode, SurvivesOneByteAtATimeReads) {
    std::string wire = "*3\r\n$5\r\nhello\r\n:42\r\n$5\r\nworld\r\n";
    Reply r = decode(wire, /*chunk_size=*/1);
    const auto& arr = r.as_array();
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr[0].as_string(), "hello");
    EXPECT_EQ(arr[1].as_int(), 42);
    EXPECT_EQ(arr[2].as_string(), "world");
}

TEST(RespDecode, SurvivesThreeByteChunks) {
    std::string wire = "*2\r\n$20\r\n01234567890123456789\r\n:7\r\n";
    Reply r = decode(wire, /*chunk_size=*/3);
    const auto& arr = r.as_array();
    EXPECT_EQ(arr[0].as_string(), "01234567890123456789");
    EXPECT_EQ(arr[1].as_int(), 7);
}

TEST(RespDecode, UnknownTypeByteThrowsProtocolError) {
    EXPECT_THROW(decode("^weird\r\n"), hotkv::ProtocolError);
}

TEST(RespDecode, ConnectionClosedMidReplyThrowsConnectionError) {
    FakeStream stream({"$5\r\nhel"}); // truncated: promises 5 bytes, delivers 3 then EOF
    RespReader reader(stream);
    EXPECT_THROW(reader.read_reply(), hotkv::ConnectionError);
}

TEST(RespDecode, AbsurdBulkLengthIsAProtocolErrorNotAnAllocation) {
    hotkv::testutil::FakeStream stream({"$99999999999999\r\nx\r\n"});
    hotkv::detail::RespReader reader(stream);
    EXPECT_THROW(reader.read_reply(), hotkv::ProtocolError);
}

TEST(RespDecode, AbsurdArrayLengthFailsAtEofWithoutHugeReservation) {
    hotkv::testutil::FakeStream stream({"*2000000000\r\n:1\r\n"});
    hotkv::detail::RespReader reader(stream);
    EXPECT_THROW(reader.read_reply(), hotkv::ConnectionError); // ran out of data
}

TEST(RespDecode, ArrayLengthBeyondTheSanityLimitIsAProtocolError) {
    hotkv::testutil::FakeStream stream({"*99999999999999\r\n"});
    hotkv::detail::RespReader reader(stream);
    EXPECT_THROW(reader.read_reply(), hotkv::ProtocolError);
}

TEST(RespDecode, DeeplyNestedReplyIsRefusedInsteadOfOverflowingTheStack) {
    std::string deep;
    for (int i = 0; i < 100000; ++i) {
        deep += "*1\r\n";
    }
    deep += ":1\r\n";
    hotkv::testutil::FakeStream stream({deep});
    hotkv::detail::RespReader reader(stream);
    EXPECT_THROW(reader.read_reply(), hotkv::ProtocolError);
}
