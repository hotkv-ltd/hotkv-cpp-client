// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

namespace {

const char* condition_keyword(ExpireCondition cond) {
    switch (cond) {
        case ExpireCondition::Nx:
            return "NX";
        case ExpireCondition::Xx:
            return "XX";
        case ExpireCondition::Gt:
            return "GT";
        case ExpireCondition::Lt:
            return "LT";
        case ExpireCondition::None:
        default:
            return nullptr;
    }
}

std::vector<int64_t> as_int_list(const Reply& r) {
    std::vector<int64_t> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_int());
    }
    return out;
}

} // namespace

int64_t Client::del(const std::vector<std::string>& keys) {
    std::vector<std::string> args{"DEL"};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

int64_t Client::unlink(const std::vector<std::string>& keys) {
    std::vector<std::string> args{"UNLINK"};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

int64_t Client::exists(const std::vector<std::string>& keys) {
    std::vector<std::string> args{"EXISTS"};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

std::string Client::type(std::string_view key) {
    return command({"TYPE", std::string(key)}).as_string();
}

bool Client::expire(std::string_view key, int64_t seconds, ExpireCondition cond) {
    std::vector<std::string> args{"EXPIRE", std::string(key), std::to_string(seconds)};
    if (const char* kw = condition_keyword(cond)) {
        args.emplace_back(kw);
    }
    return command(std::move(args)).as_int() != 0;
}

bool Client::pexpire(std::string_view key, int64_t milliseconds, ExpireCondition cond) {
    std::vector<std::string> args{"PEXPIRE", std::string(key), std::to_string(milliseconds)};
    if (const char* kw = condition_keyword(cond)) {
        args.emplace_back(kw);
    }
    return command(std::move(args)).as_int() != 0;
}

bool Client::expireat(std::string_view key, int64_t unix_seconds, ExpireCondition cond) {
    std::vector<std::string> args{"EXPIREAT", std::string(key), std::to_string(unix_seconds)};
    if (const char* kw = condition_keyword(cond)) {
        args.emplace_back(kw);
    }
    return command(std::move(args)).as_int() != 0;
}

bool Client::pexpireat(std::string_view key, int64_t unix_milliseconds, ExpireCondition cond) {
    std::vector<std::string> args{"PEXPIREAT", std::string(key), std::to_string(unix_milliseconds)};
    if (const char* kw = condition_keyword(cond)) {
        args.emplace_back(kw);
    }
    return command(std::move(args)).as_int() != 0;
}

int64_t Client::ttl(std::string_view key) {
    return command({"TTL", std::string(key)}).as_int();
}

int64_t Client::pttl(std::string_view key) {
    return command({"PTTL", std::string(key)}).as_int();
}

bool Client::persist(std::string_view key) {
    return command({"PERSIST", std::string(key)}).as_int() != 0;
}

std::vector<std::string> Client::keys(std::string_view pattern) {
    return command({"KEYS", std::string(pattern)}).as_string_array();
}

std::optional<std::string> Client::randomkey() {
    return command({"RANDOMKEY"}).as_optional_string();
}

bool Client::rename(std::string_view src, std::string_view dst) {
    command({"RENAME", std::string(src), std::string(dst)});
    return true;
}

bool Client::renamenx(std::string_view src, std::string_view dst) {
    return command({"RENAMENX", std::string(src), std::string(dst)}).as_int() != 0;
}

int64_t Client::touch(const std::vector<std::string>& keys) {
    std::vector<std::string> args{"TOUCH"};
    args.insert(args.end(), keys.begin(), keys.end());
    return command(std::move(args)).as_int();
}

std::optional<std::string> Client::dump(std::string_view key) {
    return command({"DUMP", std::string(key)}).as_optional_string();
}

void Client::restore(std::string_view key, int64_t ttl_ms, const std::string& serialized, bool replace) {
    std::vector<std::string> args{"RESTORE", std::string(key), std::to_string(ttl_ms), serialized};
    if (replace) {
        args.emplace_back("REPLACE");
    }
    command(std::move(args));
}

bool Client::copy(std::string_view src, std::string_view dst, std::optional<int> dest_db, bool replace) {
    std::vector<std::string> args{"COPY", std::string(src), std::string(dst)};
    if (dest_db.has_value()) {
        args.emplace_back("DB");
        args.push_back(std::to_string(*dest_db));
    }
    if (replace) {
        args.emplace_back("REPLACE");
    }
    return command(std::move(args)).as_int() != 0;
}

bool Client::move(std::string_view key, int db) {
    return command({"MOVE", std::string(key), std::to_string(db)}).as_int() != 0;
}

std::optional<std::string> Client::object_encoding(std::string_view key) {
    return command({"OBJECT", "ENCODING", std::string(key)}).as_optional_string();
}

bool Client::nexpire(std::string_view key, int64_t nanoseconds) {
    return command({"NEXPIRE", std::string(key), std::to_string(nanoseconds)}).as_int() != 0;
}

int64_t Client::nttl(std::string_view key) {
    return command({"NTTL", std::string(key)}).as_int();
}

bool Client::nexpireat(std::string_view key, int64_t unix_nanoseconds) {
    return command({"NEXPIREAT", std::string(key), std::to_string(unix_nanoseconds)}).as_int() != 0;
}

namespace {

std::vector<int64_t> hash_field_ttl_command(Client& client, const char* cmd, std::string_view key,
                                             std::optional<int64_t> amount, const std::vector<std::string>& fields,
                                             ExpireCondition cond) {
    std::vector<std::string> args{cmd, std::string(key)};
    if (amount.has_value()) {
        args.push_back(std::to_string(*amount));
    }
    if (const char* kw = condition_keyword(cond); kw != nullptr) {
        args.emplace_back(kw);
    }
    args.emplace_back("FIELDS");
    args.push_back(std::to_string(fields.size()));
    args.insert(args.end(), fields.begin(), fields.end());
    return as_int_list(client.command(std::move(args)));
}

} // namespace

std::vector<int64_t> Client::hexpire(std::string_view key, int64_t seconds, const std::vector<std::string>& fields,
                                      ExpireCondition cond) {
    return hash_field_ttl_command(*this, "HEXPIRE", key, seconds, fields, cond);
}

std::vector<int64_t> Client::hpexpire(std::string_view key, int64_t milliseconds,
                                       const std::vector<std::string>& fields, ExpireCondition cond) {
    return hash_field_ttl_command(*this, "HPEXPIRE", key, milliseconds, fields, cond);
}

std::vector<int64_t> Client::hexpireat(std::string_view key, int64_t unix_seconds,
                                        const std::vector<std::string>& fields, ExpireCondition cond) {
    return hash_field_ttl_command(*this, "HEXPIREAT", key, unix_seconds, fields, cond);
}

std::vector<int64_t> Client::hpexpireat(std::string_view key, int64_t unix_milliseconds,
                                         const std::vector<std::string>& fields, ExpireCondition cond) {
    return hash_field_ttl_command(*this, "HPEXPIREAT", key, unix_milliseconds, fields, cond);
}

std::vector<int64_t> Client::httl(std::string_view key, const std::vector<std::string>& fields) {
    return hash_field_ttl_command(*this, "HTTL", key, std::nullopt, fields, ExpireCondition::None);
}

std::vector<int64_t> Client::hpttl(std::string_view key, const std::vector<std::string>& fields) {
    return hash_field_ttl_command(*this, "HPTTL", key, std::nullopt, fields, ExpireCondition::None);
}

std::vector<int64_t> Client::hpersist(std::string_view key, const std::vector<std::string>& fields) {
    return hash_field_ttl_command(*this, "HPERSIST", key, std::nullopt, fields, ExpireCondition::None);
}

} // namespace hotkv
