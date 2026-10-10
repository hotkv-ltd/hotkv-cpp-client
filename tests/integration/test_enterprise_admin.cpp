// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

using hotkv::testutil::IntegrationTest;

// OBSERVE / ALERTS need a license with the observability feature; without
// it these skip cleanly.

TEST_F(IntegrationTest, ObserveInfoAndMetrics) {
    hotkv::ObserveInfo info;
    HOTKV_SKIP_IF_NOT_LICENSED(info = client_.observe().info(), "enterprise-observability");
    EXPECT_GE(info.uptime_seconds, 0);
    EXPECT_GE(info.connected_clients, 1);

    auto metrics = client_.observe().metrics();
    EXPECT_GE(metrics.total_commands, 1);
}

TEST_F(IntegrationTest, AlertsInfoReflectsConfiguration) {
    hotkv::AlertsInfo info;
    HOTKV_SKIP_IF_NOT_LICENSED(info = client_.alerts().info(), "enterprise-observability");
    EXPECT_FALSE(info.supported_providers.empty());
}

TEST_F(IntegrationTest, AlertsSeverityAndToggle) {
    hotkv::AlertsInfo before;
    HOTKV_SKIP_IF_NOT_LICENSED(before = client_.alerts().info(), "enterprise-observability");
    client_.alerts().set_severity("warning");
    client_.alerts().set_severity(before.min_severity.empty() ? "info" : before.min_severity);

    client_.alerts().on();
    client_.alerts().off();
    client_.alerts().on();
}

TEST_F(IntegrationTest, AlertsTestRequiresWebhookOrRejectsCleanly) {
    // ALERTS TEST either succeeds (a webhook URL is configured), fails
    // with a clear ERR (alerting not enabled / severity filtered), or
    // this whole feature is not licensed; all are acceptable outcomes
    // for this SDK-level check.
    try {
        client_.alerts().test("info", "hotkv-cpp integration test");
    } catch (const hotkv::NotLicensedError&) {
        GTEST_SKIP() << "server license does not include 'enterprise-observability'";
    } catch (const hotkv::ServerError&) {
        SUCCEED() << "server rejected the test alert with a typed error, as expected without a configured webhook";
    }
}
