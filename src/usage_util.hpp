// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/enterprise_types.hpp>

#include <optional>
#include <string>
#include <vector>

namespace hotkv::detail {

// Appends usage's wire options to args (MODEL always first); a no-op
// when usage is nullopt. Shared by PCACHE.SET, SCACHE.SET, every keyed
// cache SET and LLMBUDGET.CONSUME.
void append_usage(std::vector<std::string>& args, const std::optional<Usage>& usage);

} // namespace hotkv::detail
