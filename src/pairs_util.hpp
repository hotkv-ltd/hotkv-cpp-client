// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/reply.hpp>

#include <string>
#include <vector>

namespace hotkv::detail {

// Helpers over Reply::as_pairs() for the many HotKV replies
// shaped as a "flat array of N pairs: name1, name2, ...": each
// looks up `name` by exact match and converts its value, defaulting to
// an empty/zero value if the field is absent or nil rather than
// throwing, since the server, not a malformed reply, decides which
// fields a given reply variant includes.
using PairList = std::vector<std::pair<std::string, Reply>>;

const Reply* pairs_find(const PairList& pairs, const char* name);
std::string pairs_string(const PairList& pairs, const char* name);
int64_t pairs_int(const PairList& pairs, const char* name);
double pairs_double(const PairList& pairs, const char* name);
bool pairs_bool(const PairList& pairs, const char* name);

} // namespace hotkv::detail
