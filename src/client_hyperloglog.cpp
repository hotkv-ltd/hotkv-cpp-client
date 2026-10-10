// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

bool Client::pfadd(std::string_view key, const std::vector<std::string>& elements) {
    std::vector<std::string> args{"PFADD", std::string(key)};
    args.insert(args.end(), elements.begin(), elements.end());
    return command(std::move(args)).as_int() != 0;
}

void Client::pfmerge(std::string_view dst, const std::vector<std::string>& sources) {
    std::vector<std::string> args{"PFMERGE", std::string(dst)};
    args.insert(args.end(), sources.begin(), sources.end());
    command(std::move(args));
}

int64_t Client::pfcount(const std::vector<std::string>& keys) {
    std::vector<std::string> args{"PFCOUNT"};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

} // namespace hotkv
