// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

int64_t Client::setbit(std::string_view key, int64_t offset, int value) {
    return command({"SETBIT", std::string(key), std::to_string(offset), std::to_string(value)}).as_int();
}

int64_t Client::getbit(std::string_view key, int64_t offset) {
    return command({"GETBIT", std::string(key), std::to_string(offset)}).as_int();
}

int64_t Client::bitcount(std::string_view key) {
    return command({"BITCOUNT", std::string(key)}).as_int();
}

int64_t Client::bitcount(std::string_view key, int64_t start, int64_t end, bool by_bit) {
    std::vector<std::string> args{"BITCOUNT", std::string(key), std::to_string(start), std::to_string(end)};
    if (by_bit) {
        args.emplace_back("BIT");
    }
    return command(std::move(args)).as_int();
}

int64_t Client::bitpos(std::string_view key, int bit, std::optional<int64_t> start, std::optional<int64_t> end,
                        bool by_bit) {
    std::vector<std::string> args{"BITPOS", std::string(key), std::to_string(bit)};
    if (start.has_value()) {
        args.push_back(std::to_string(*start));
    }
    if (end.has_value()) {
        args.push_back(std::to_string(*end));
    }
    if (by_bit) {
        args.emplace_back("BIT");
    }
    return command(std::move(args)).as_int();
}

namespace {
int64_t bitop(Client& client, const char* op, std::string_view dst, const std::vector<std::string>& keys) {
    std::vector<std::string> args{"BITOP", op, std::string(dst)};
    args.insert(args.end(), keys.begin(), keys.end());
    return client.command(std::move(args)).as_int();
}
} // namespace

int64_t Client::bitop_and(std::string_view dst, const std::vector<std::string>& keys) {
    return bitop(*this, "AND", dst, keys);
}

int64_t Client::bitop_or(std::string_view dst, const std::vector<std::string>& keys) {
    return bitop(*this, "OR", dst, keys);
}

int64_t Client::bitop_xor(std::string_view dst, const std::vector<std::string>& keys) {
    return bitop(*this, "XOR", dst, keys);
}

int64_t Client::bitop_not(std::string_view dst, std::string_view key) {
    return bitop(*this, "NOT", dst, {std::string(key)});
}

std::vector<std::optional<int64_t>> Client::bitfield(std::string_view key, const std::vector<std::string>& ops) {
    std::vector<std::string> args{"BITFIELD", std::string(key)};
    args.insert(args.end(), ops.begin(), ops.end());
    Reply r = command(std::move(args));
    std::vector<std::optional<int64_t>> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_optional_int());
    }
    return out;
}

} // namespace hotkv
