// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "pairs_util.hpp"

namespace hotkv::detail {

const Reply* pairs_find(const PairList& pairs, const char* name) {
    for (const auto& p : pairs) {
        if (p.first == name) {
            return &p.second;
        }
    }
    return nullptr;
}

std::string pairs_string(const PairList& pairs, const char* name) {
    const Reply* r = pairs_find(pairs, name);
    if (r == nullptr || r->is_nil()) {
        return {};
    }
    return r->as_string();
}

int64_t pairs_int(const PairList& pairs, const char* name) {
    const Reply* r = pairs_find(pairs, name);
    if (r == nullptr || r->is_nil()) {
        return 0;
    }
    return r->as_int();
}

double pairs_double(const PairList& pairs, const char* name) {
    const Reply* r = pairs_find(pairs, name);
    if (r == nullptr || r->is_nil()) {
        return 0.0;
    }
    return r->as_double();
}

bool pairs_bool(const PairList& pairs, const char* name) {
    return pairs_int(pairs, name) != 0;
}

} // namespace hotkv::detail
