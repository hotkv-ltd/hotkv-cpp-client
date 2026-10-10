// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

// Compiled against an installed hotkv-cpp (CMake package or pkg-config).
// Needs no server; with HOTKV_TEST_URL set it also PINGs it.

#include <hotkv/hotkv.hpp>

#include <cstdlib>
#include <iostream>

int main() {
    if (hotkv::key_slot("foo") != 12182) {
        std::cerr << "key_slot mismatch\n";
        return 1;
    }
    hotkv::Options opts = hotkv::Options::parse_url("hotkvs://user:pw@example.com:6400/2?pool_size=4");
    if (opts.host != "example.com" || opts.port != 6400 || opts.database != 2 || !opts.tls.enabled) {
        std::cerr << "url parse mismatch\n";
        return 1;
    }
    if (hotkv::detail::format_usd(1e-5) != "0.00001") {
        std::cerr << "format mismatch\n";
        return 1;
    }
    if (const char* url = std::getenv("HOTKV_TEST_URL"); url != nullptr && *url != '\0') {
        hotkv::Client client = hotkv::Client::connect(url);
        if (!client.ping()) {
            std::cerr << "ping failed\n";
            return 1;
        }
    }
    std::cout << "hotkv-cpp consumer ok\n";
    return 0;
}
