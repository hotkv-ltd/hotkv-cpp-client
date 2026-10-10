// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "usage_util.hpp"

namespace hotkv::detail {

void append_usage(std::vector<std::string>& args, const std::optional<Usage>& usage) {
    if (!usage.has_value()) {
        return;
    }
    args.emplace_back("MODEL");
    args.push_back(usage->model);
    if (usage->input_tokens > 0) {
        args.emplace_back("INPUT");
        args.push_back(std::to_string(usage->input_tokens));
    }
    if (usage->output_tokens > 0) {
        args.emplace_back("OUTPUT");
        args.push_back(std::to_string(usage->output_tokens));
    }
    if (usage->cache_read_tokens > 0) {
        args.emplace_back("CACHEREAD");
        args.push_back(std::to_string(usage->cache_read_tokens));
    }
    if (usage->cache_write_5m_tokens > 0) {
        args.emplace_back("CACHEWRITE5M");
        args.push_back(std::to_string(usage->cache_write_5m_tokens));
    }
    if (usage->cache_write_1h_tokens > 0) {
        args.emplace_back("CACHEWRITE1H");
        args.push_back(std::to_string(usage->cache_write_1h_tokens));
    }
}

} // namespace hotkv::detail
