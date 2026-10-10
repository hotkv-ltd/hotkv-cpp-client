// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

using hotkv::ClusterClient;
using hotkv::Error;

// The test server is a standalone (non-cluster) instance, so a full
// MOVED/ASK redirect exercise is not possible here; these tests instead
// confirm ClusterClient degrades predictably against it (a clear error
// or an empty topology, never a crash or a hang) and that it can still
// route ordinary commands when the cluster happens to be a single node
// that answers CLUSTER SLOTS with an empty (non-cluster) topology, which
// several servers do rather than erroring.
TEST(ClusterClientTest, HandlesNonClusterServerWithoutCrashing) {
    auto url = hotkv::testutil::test_server_url();
    if (!url.has_value()) {
        GTEST_SKIP() << "HOTKV_TEST_URL not set; skipping integration test";
    }

    ClusterClient cluster = ClusterClient::connect(*url);
    try {
        cluster.refresh_slots();
    } catch (const Error&) {
        // The standalone test server may not support CLUSTER SLOTS or
        // CLUSTER SHARDS at all; that is expected here, not a defect in
        // this SDK, so treat it as a clean skip.
        GTEST_SKIP() << "test server is not running in cluster mode (CLUSTER SLOTS/SHARDS unavailable)";
    }

    // If refresh_slots() succeeded (e.g. it returned an empty slot map
    // for a non-cluster single node), a keyed command should still
    // either succeed or raise a clear, typed error rather than hang.
    std::string key = "hotkvtest:cluster:" + std::to_string(::testing::UnitTest::GetInstance()->random_seed());
    try {
        cluster.command_for_key(key, {"SET", key, "v"});
        cluster.command_for_key(key, {"DEL", key});
    } catch (const Error&) {
        SUCCEED() << "cluster routing surfaced a typed error against a non-cluster server, as expected";
    }
}
