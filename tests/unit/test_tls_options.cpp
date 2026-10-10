// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#ifndef _WIN32

#include "fake_server.hpp"

#include <hotkv/hotkv.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

hotkv::Options tls_to(const hotkv::testutil::FakeServer& s) {
    hotkv::Options o;
    o.host = "127.0.0.1";
    o.port = s.port();
    o.tls.enabled = true;
    o.connect_timeout = std::chrono::milliseconds(1000);
    o.command_timeout = std::chrono::milliseconds(1000);
    return o;
}

} // namespace

// An explicit CA file that cannot be loaded must be an error. Falling back
// to the system trust store instead would silently widen who is trusted.
TEST(TlsOptions, UnreadableCaFileIsAnErrorNotAFallback) {
    hotkv::testutil::FakeServer server([](int, const std::vector<std::string>&) { return std::string("+OK\r\n"); });
    hotkv::Options o = tls_to(server);
    o.tls.ca_file = "/nonexistent/definitely-not-a-ca.pem";
    try {
        hotkv::Client::connect(o).ping();
        FAIL() << "expected a ConnectionError";
    } catch (const hotkv::ConnectionError& e) {
        std::string what = e.what();
        // Built with TLS: names the CA file; built without: says so.
        EXPECT_TRUE(what.find("CA file") != std::string::npos || what.find("HOTKV_CPP_WITH_TLS") != std::string::npos)
            << what;
    }
}

TEST(TlsOptions, TlsAgainstAPlainServerFailsFastWithAConnectionError) {
    hotkv::testutil::FakeServer server([](int, const std::vector<std::string>&) { return std::string("+OK\r\n"); });
    hotkv::Options o = tls_to(server);
    o.tls.insecure_skip_verify = true;
    EXPECT_THROW(hotkv::Client::connect(o).ping(), hotkv::ConnectionError);
}

#endif // !_WIN32
