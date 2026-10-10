// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

namespace {

void append_scan_options(std::vector<std::string>& args, const ScanOptions& opts, bool allow_type) {
    if (!opts.match.empty()) {
        args.emplace_back("MATCH");
        args.push_back(opts.match);
    }
    if (opts.count > 0) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(opts.count));
    }
    if (allow_type && !opts.type.empty()) {
        args.emplace_back("TYPE");
        args.push_back(opts.type);
    }
}

} // namespace

Scanner Client::scan(const ScanOptions& opts) {
    Client self = *this;
    ScanOptions captured = opts;
    return Scanner(
        [self, captured](const std::string& cursor) mutable {
            std::vector<std::string> args{"SCAN", cursor};
            append_scan_options(args, captured, /*allow_type=*/true);
            return self.command(std::move(args));
        },
        1);
}

Scanner Client::sscan(std::string_view key, const ScanOptions& opts) {
    Client self = *this;
    std::string key_str(key);
    ScanOptions captured = opts;
    return Scanner(
        [self, key_str, captured](const std::string& cursor) mutable {
            std::vector<std::string> args{"SSCAN", key_str, cursor};
            append_scan_options(args, captured, /*allow_type=*/false);
            return self.command(std::move(args));
        },
        1);
}

Scanner Client::hscan(std::string_view key, const ScanOptions& opts) {
    Client self = *this;
    std::string key_str(key);
    ScanOptions captured = opts;
    return Scanner(
        [self, key_str, captured](const std::string& cursor) mutable {
            std::vector<std::string> args{"HSCAN", key_str, cursor};
            append_scan_options(args, captured, /*allow_type=*/false);
            return self.command(std::move(args));
        },
        2);
}

Scanner Client::zscan(std::string_view key, const ScanOptions& opts) {
    Client self = *this;
    std::string key_str(key);
    ScanOptions captured = opts;
    return Scanner(
        [self, key_str, captured](const std::string& cursor) mutable {
            std::vector<std::string> args{"ZSCAN", key_str, cursor};
            append_scan_options(args, captured, /*allow_type=*/false);
            return self.command(std::move(args));
        },
        2);
}

} // namespace hotkv
