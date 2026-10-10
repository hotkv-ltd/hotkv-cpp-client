// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

// TLS, mutual TLS and password authentication against real servers. Every
// test skips unless its environment is provided:
//
//   HOTKV_TEST_TLS_HOST       host of the TLS listeners (default 127.0.0.1)
//   HOTKV_TEST_TLS_CA         CA bundle (PEM) that signed the server certificate
//   HOTKV_TEST_TLS_MTLS_PORT  TLS port of a server with tls-auth-clients yes
//   HOTKV_TEST_TLS_CERT/KEY   a client certificate and key that CA signed
//   HOTKV_TEST_TLS_AUTH_PORT  TLS port of a server with requirepass and
//                             tls-auth-clients no
//   HOTKV_TEST_AUTH_PASSWORD  that server's password
//
// The server certificate must name HOTKV_TEST_TLS_HOST (as a DNS or IP
// subject alternative name).

#include "test_helpers.hpp"

#include <chrono>
#include <cstdlib>
#include <string>

namespace {

std::string env(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : fallback;
}

struct TlsEnv {
    std::string host = env("HOTKV_TEST_TLS_HOST", "127.0.0.1");
    std::string ca = env("HOTKV_TEST_TLS_CA");
    std::string mtls_port = env("HOTKV_TEST_TLS_MTLS_PORT");
    std::string cert = env("HOTKV_TEST_TLS_CERT");
    std::string key = env("HOTKV_TEST_TLS_KEY");
    std::string auth_port = env("HOTKV_TEST_TLS_AUTH_PORT");
    std::string password = env("HOTKV_TEST_AUTH_PASSWORD");
};

hotkv::Options tls_options(const TlsEnv& e, const std::string& port) {
    hotkv::Options o;
    o.host = e.host;
    o.port = static_cast<uint16_t>(std::stoi(port));
    o.tls.enabled = true;
    o.tls.ca_file = e.ca;
    o.connect_timeout = std::chrono::milliseconds(3000);
    o.command_timeout = std::chrono::milliseconds(3000);
    o.pool_size = 2;
    return o;
}

// Skips when this build has no TLS support.
#define SKIP_IF_BUILT_WITHOUT_TLS(opts)                                                        \
    try {                                                                                      \
        hotkv::Client::connect(opts).ping();                                                   \
    } catch (const hotkv::ConnectionError& e_) {                                               \
        if (std::string(e_.what()).find("HOTKV_CPP_WITH_TLS") != std::string::npos) {          \
            GTEST_SKIP() << "built without TLS support";                                       \
        }                                                                                      \
    } catch (...) {                                                                            \
    }

} // namespace

TEST(TlsAuth, MutualTlsRoundTrip) {
    TlsEnv e;
    if (e.ca.empty() || e.mtls_port.empty() || e.cert.empty() || e.key.empty()) {
        GTEST_SKIP() << "HOTKV_TEST_TLS_CA/MTLS_PORT/CERT/KEY not set";
    }
    hotkv::Options o = tls_options(e, e.mtls_port);
    o.tls.cert_file = e.cert;
    o.tls.key_file = e.key;
    SKIP_IF_BUILT_WITHOUT_TLS(o);
    hotkv::Client c = hotkv::Client::connect(o);
    EXPECT_TRUE(c.ping());
    std::string k = hotkv::testutil::unique_prefix("tls");
    EXPECT_TRUE(c.set(k, "over-tls"));
    EXPECT_EQ(c.get(k), std::optional<std::string>("over-tls"));
    // A payload spanning many TLS records.
    std::string big(300000, 'z');
    c.set(k, big);
    EXPECT_EQ(c.get(k), std::optional<std::string>(big));
    c.del({k});

    // The URL form of the same connection.
    std::string url = "hotkvs://" + e.host + ":" + e.mtls_port + "?tls_ca_file=" + e.ca + "&tls_cert_file=" + e.cert +
                      "&tls_key_file=" + e.key;
    EXPECT_TRUE(hotkv::Client::connect(url).ping());
}

TEST(TlsAuth, MissingClientCertificateIsRefused) {
    TlsEnv e;
    if (e.ca.empty() || e.mtls_port.empty()) {
        GTEST_SKIP() << "HOTKV_TEST_TLS_CA/MTLS_PORT not set";
    }
    hotkv::Options o = tls_options(e, e.mtls_port);
    SKIP_IF_BUILT_WITHOUT_TLS(tls_options(e, e.mtls_port));
    // TLS 1.3 reports the rejected client certificate on the first read.
    EXPECT_THROW(hotkv::Client::connect(o).ping(), hotkv::ConnectionError);
}

TEST(TlsAuth, UntrustedCaFailsVerification) {
    TlsEnv e;
    if (e.ca.empty() || e.mtls_port.empty() || e.cert.empty()) {
        GTEST_SKIP() << "HOTKV_TEST_TLS_CA/MTLS_PORT/CERT not set";
    }
    SKIP_IF_BUILT_WITHOUT_TLS(tls_options(e, e.mtls_port));
    hotkv::Options o = tls_options(e, e.mtls_port);
    o.tls.ca_file = e.cert; // a leaf certificate, not the signing CA
    o.tls.cert_file = e.cert;
    o.tls.key_file = e.key;
    EXPECT_THROW(hotkv::Client::connect(o).ping(), hotkv::ConnectionError);
}

TEST(TlsAuth, ServerNameMismatchFailsAndSkipVerifyOverridesIt) {
    TlsEnv e;
    if (e.ca.empty() || e.auth_port.empty() || e.password.empty()) {
        GTEST_SKIP() << "HOTKV_TEST_TLS_CA/AUTH_PORT/AUTH_PASSWORD not set";
    }
    hotkv::Options o = tls_options(e, e.auth_port);
    o.password = e.password;
    SKIP_IF_BUILT_WITHOUT_TLS(o);
    EXPECT_TRUE(hotkv::Client::connect(o).ping()); // control: verified and authenticated

    o.tls.server_name = "not-the-server.example";
    EXPECT_THROW(hotkv::Client::connect(o).ping(), hotkv::ConnectionError);

    hotkv::Options insecure = tls_options(e, e.auth_port);
    insecure.password = e.password;
    insecure.tls.ca_file.clear();
    insecure.tls.insecure_skip_verify = true;
    EXPECT_TRUE(hotkv::Client::connect(insecure).ping());
}

TEST(TlsAuth, PasswordOverTlsAndWrongPasswordFailsCleanly) {
    TlsEnv e;
    if (e.ca.empty() || e.auth_port.empty() || e.password.empty()) {
        GTEST_SKIP() << "HOTKV_TEST_TLS_CA/AUTH_PORT/AUTH_PASSWORD not set";
    }
    hotkv::Options good = tls_options(e, e.auth_port);
    good.password = e.password;
    SKIP_IF_BUILT_WITHOUT_TLS(good);
    hotkv::Client c = hotkv::Client::connect(good);
    EXPECT_TRUE(c.ping());
    std::string k = hotkv::testutil::unique_prefix("tlsauth");
    c.set(k, "authed");
    EXPECT_EQ(c.get(k), std::optional<std::string>("authed"));
    c.del({k});

    hotkv::Options bad = good;
    bad.password = "definitely-not-the-password";
    try {
        hotkv::Client::connect(bad).ping();
        FAIL() << "expected an authentication failure";
    } catch (const hotkv::ServerError& err) {
        EXPECT_TRUE(err.code() == "WRONGPASS" || err.code() == "ERR") << err.code();
    }

    hotkv::Options none = good;
    none.password.clear();
    try {
        hotkv::Client::connect(none).ping();
        FAIL() << "expected NOAUTH";
    } catch (const hotkv::ServerError& err) {
        EXPECT_EQ(err.code(), "NOAUTH");
    }

    // URL form with credentials.
    std::string url = "hotkvs://:" + e.password + "@" + e.host + ":" + e.auth_port + "?tls_ca_file=" + e.ca;
    EXPECT_TRUE(hotkv::Client::connect(url).ping());
}

TEST(TlsAuth, PlainClientToTlsPortFailsWithoutHanging) {
    TlsEnv e;
    if (e.auth_port.empty()) {
        GTEST_SKIP() << "HOTKV_TEST_TLS_AUTH_PORT not set";
    }
    hotkv::Options o;
    o.host = e.host;
    o.port = static_cast<uint16_t>(std::stoi(e.auth_port));
    o.connect_timeout = std::chrono::milliseconds(1500);
    o.command_timeout = std::chrono::milliseconds(1500);
    auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(hotkv::Client::connect(o).ping(), hotkv::Error);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
}
