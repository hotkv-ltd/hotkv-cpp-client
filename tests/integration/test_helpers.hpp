// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/hotkv.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace hotkv::testutil {

// HOTKV_TEST_URL if set and non-empty, else nullopt.
std::optional<std::string> test_server_url();

// A fresh Client connected to the test server. Only valid to call once
// test_server_url() is known to have a value.
Client make_test_client();

// A short-lived key/namespace prefix unique to this call, so concurrent
// test runs never collide. Every integration test builds its keys under
// one of these.
std::string unique_prefix(const std::string& label);

// Shared fixture for every integration test file: skips (in SetUp) when
// HOTKV_TEST_URL is unset, connects a Client, and hands out uniquely
// prefixed keys via key(), each of which is DELed in TearDown so tests
// clean up after themselves even on failure.
class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto url = test_server_url();
        if (!url.has_value()) {
            GTEST_SKIP() << "HOTKV_TEST_URL not set; skipping integration test";
        }
        client_ = Client::connect(*url);
        prefix_ = unique_prefix(test_name());
    }

    void TearDown() override {
        if (!created_keys_.empty()) {
            try {
                client_.del(created_keys_);
            } catch (...) {
                // best-effort cleanup only
            }
        }
    }

    static std::string test_name() { return ::testing::UnitTest::GetInstance()->current_test_info()->name(); }

    // Builds a unique key under this test's prefix and remembers it for
    // cleanup in TearDown().
    std::string key(const std::string& suffix) {
        std::string k = prefix_ + ":" + suffix;
        created_keys_.push_back(k);
        return k;
    }

    Client client_{Options{}};
    std::string prefix_;
    std::vector<std::string> created_keys_;
};

} // namespace hotkv::testutil

// Skips the current test, with a clear reason, unless HOTKV_TEST_URL is
// set; otherwise declares a local std::string named `url_var` holding it.
#define HOTKV_SKIP_IF_NO_SERVER(url_var)                                         \
    auto url_var##_opt_ = hotkv::testutil::test_server_url();                    \
    if (!url_var##_opt_.has_value()) {                                          \
        GTEST_SKIP() << "HOTKV_TEST_URL not set; skipping integration test";     \
    }                                                                            \
    std::string url_var = *url_var##_opt_

// Runs `expr` and, if it throws hotkv::NotLicensedError, skips the test
// with a reason naming the feature; otherwise lets any other exception
// propagate (a real failure) and the successful result flow through.
// On an unlicensed server every enterprise family hits this; each check
// starts passing for real once a license is loaded.
#define HOTKV_SKIP_IF_NOT_LICENSED(expr, feature_name)                                            \
    try {                                                                                          \
        (void)(expr);                                                                              \
    } catch (const hotkv::NotLicensedError&) {                                                     \
        GTEST_SKIP() << "server license does not include '" << feature_name << "'";                \
    }
