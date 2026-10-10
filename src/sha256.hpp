// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <string>
#include <string_view>

namespace hotkv::detail {

// Lowercase hex SHA-256 digest (64 characters) of data. Implemented
// in-library (FIPS 180-4) so the SDK has no required runtime dependency
// on OpenSSL; used by ai::build_prompt_cache_key.
std::string sha256_hex(std::string_view data);

} // namespace hotkv::detail
