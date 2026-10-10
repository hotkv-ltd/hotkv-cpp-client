// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/reply.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace hotkv {

// Pull-style cursor over a SCAN/SSCAN/HSCAN/ZSCAN sweep. Construct via
// Client::scan()/sscan()/hscan()/zscan(); call next() (or next_scored()
// for zscan()) repeatedly until it returns false. A Scanner issues one
// SCAN-family command per page, lazily, as it is drained; it does not
// snapshot the keyspace, so it follows the same "may see a key zero,
// once, or more than once" guarantee as the underlying command. Not
// thread-safe: use each Scanner from a single thread.
class Scanner {
public:
    // Constructed by Client; `issue` sends one SCAN-family call for the
    // given cursor and returns its [next_cursor, items] reply. `stride`
    // is 1 for SCAN/SSCAN (one item per entry) or 2 for HSCAN/ZSCAN
    // (field+value / member+score pairs).
    Scanner(std::function<Reply(const std::string&)> issue, int stride);

    bool next(std::string& value);
    bool next(std::string& first, std::string& second);
    bool next_scored(std::string& member, double& score);

private:
    bool advance_if_needed();

    std::function<Reply(const std::string&)> issue_;
    int stride_;
    std::vector<std::string> page_;
    std::size_t idx_ = 0;
    std::string cursor_ = "0";
    bool started_ = false;
    bool finished_ = false;
};

} // namespace hotkv
