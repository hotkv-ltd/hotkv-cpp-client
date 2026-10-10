// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <string>
#include <string_view>

namespace hotkv::detail {

// Lowercase hex SHA-1 digest (40 characters) of data. HotKV
// scripts are addressed by the SHA-1 of their source, so EVALSHA-with-
// fallback needs this locally to avoid a round trip through SCRIPT LOAD
// just to learn the hash. Not used anywhere security sensitive.
std::string sha1_hex(std::string_view data);

} // namespace hotkv::detail
