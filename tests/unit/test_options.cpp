// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/options.hpp>

#include <hotkv/error.hpp>

#include <gtest/gtest.h>

using hotkv::Options;

TEST(OptionsParseUrl, BasicHostPort) {
    Options o = Options::parse_url("hotkv://127.0.0.1:6399");
    EXPECT_EQ(o.host, "127.0.0.1");
    EXPECT_EQ(o.port, 6399);
    EXPECT_FALSE(o.tls.enabled);
    EXPECT_EQ(o.database, 0);
}

TEST(OptionsParseUrl, DefaultsWhenHostPortOmitted) {
    Options o = Options::parse_url("hotkv://");
    EXPECT_EQ(o.host, "127.0.0.1");
    EXPECT_EQ(o.port, 6379);
}

TEST(OptionsParseUrl, TlsSchemeEnablesTls) {
    Options o = Options::parse_url("hotkvs://example.com:6400");
    EXPECT_TRUE(o.tls.enabled);
    EXPECT_EQ(o.tls.server_name, "example.com");
}

TEST(OptionsParseUrl, RedisSchemesAcceptedForMigration) {
    Options plain = Options::parse_url("redis://localhost:6379");
    EXPECT_FALSE(plain.tls.enabled);
    Options tls = Options::parse_url("rediss://localhost:6380");
    EXPECT_TRUE(tls.tls.enabled);
}

TEST(OptionsParseUrl, UsernameAndPassword) {
    Options o = Options::parse_url("hotkv://alice:s3cret@127.0.0.1:6399");
    EXPECT_EQ(o.username, "alice");
    EXPECT_EQ(o.password, "s3cret");
}

TEST(OptionsParseUrl, LegacySinglePasswordForm) {
    Options o = Options::parse_url("hotkv://s3cret@127.0.0.1:6399");
    EXPECT_TRUE(o.username.empty());
    EXPECT_EQ(o.password, "s3cret");
}

TEST(OptionsParseUrl, DatabaseIndexFromPath) {
    Options o = Options::parse_url("hotkv://127.0.0.1:6399/3");
    EXPECT_EQ(o.database, 3);
}

TEST(OptionsParseUrl, QueryParametersOverrideDefaults) {
    Options o = Options::parse_url(
        "hotkv://127.0.0.1:6399?client_name=myapp&connect_timeout_ms=1500&pool_size=16&resp3=false");
    EXPECT_EQ(o.client_name, "myapp");
    EXPECT_EQ(o.connect_timeout.count(), 1500);
    EXPECT_EQ(o.pool_size, 16u);
    EXPECT_FALSE(o.use_resp3);
}

TEST(OptionsParseUrl, TlsQueryParameters) {
    Options o = Options::parse_url(
        "hotkv://127.0.0.1:6399?tls_insecure_skip_verify=true&tls_ca_file=/tmp/ca.pem");
    EXPECT_TRUE(o.tls.enabled);
    EXPECT_TRUE(o.tls.insecure_skip_verify);
    EXPECT_EQ(o.tls.ca_file, "/tmp/ca.pem");
}

TEST(OptionsParseUrl, Ipv6Literal) {
    Options o = Options::parse_url("hotkv://[::1]:6399");
    EXPECT_EQ(o.host, "::1");
    EXPECT_EQ(o.port, 6399);
}

TEST(OptionsParseUrl, UnsupportedSchemeThrows) {
    EXPECT_THROW(Options::parse_url("ftp://127.0.0.1"), hotkv::Error);
}

TEST(OptionsParseUrl, MissingSchemeThrows) {
    EXPECT_THROW(Options::parse_url("127.0.0.1:6399"), hotkv::Error);
}

TEST(OptionsParseUrl, InvalidPortThrows) {
    EXPECT_THROW(Options::parse_url("hotkv://127.0.0.1:notaport"), hotkv::Error);
}

TEST(OptionsParseUrl, InvalidDatabaseIndexThrows) {
    EXPECT_THROW(Options::parse_url("hotkv://127.0.0.1:6399/notanumber"), hotkv::Error);
}

TEST(OptionsParseUrl, OutOfRangeNumbersAreRejectedNotWrapped) {
    EXPECT_THROW(Options::parse_url("hotkv://127.0.0.1:70000"), hotkv::Error);
    EXPECT_THROW(Options::parse_url("hotkv://127.0.0.1:0"), hotkv::Error);
    EXPECT_THROW(Options::parse_url("hotkv://127.0.0.1:6399/-1"), hotkv::Error);
    EXPECT_THROW(Options::parse_url("hotkv://127.0.0.1?pool_size=0"), hotkv::Error);
    EXPECT_THROW(Options::parse_url("hotkv://127.0.0.1?pool_size=-3"), hotkv::Error);
    EXPECT_THROW(Options::parse_url("hotkv://127.0.0.1?command_timeout_ms=-1"), hotkv::Error);
    EXPECT_EQ(Options::parse_url("hotkv://127.0.0.1:65535").port, 65535);
}

TEST(OptionsParseUrl, ErrorMessagesDoNotEchoACredential) {
    try {
        Options::parse_url("user:supersecret@host:6399");
        FAIL() << "expected an error";
    } catch (const hotkv::Error& e) {
        EXPECT_EQ(std::string(e.what()).find("supersecret"), std::string::npos) << e.what();
    }
}
