// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "test_helpers.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>

namespace hotkv::testutil {

std::optional<std::string> test_server_url() {
    const char* env = std::getenv("HOTKV_TEST_URL");
    if (env == nullptr || std::string(env).empty()) {
        return std::nullopt;
    }
    return std::string(env);
}

Client make_test_client() {
    auto url = test_server_url();
    return Client::connect(*url);
}

std::string unique_prefix(const std::string& label) {
    static std::atomic<uint64_t> counter{0};
    uint64_t n = counter.fetch_add(1);
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << "hotkvtest:" << label << ":" << now << ":" << n;
    return oss.str();
}

} // namespace hotkv::testutil
