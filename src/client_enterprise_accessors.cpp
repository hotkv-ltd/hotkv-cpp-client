// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

namespace hotkv {

TagCommands Client::tag() {
    return TagCommands(*this);
}

HotKeysCommands Client::hotkeys() {
    return HotKeysCommands(*this);
}

GcraCommands Client::gcra() {
    return GcraCommands(*this);
}

VsimCommands Client::vsim() {
    return VsimCommands(*this);
}

SCacheCommands Client::scache() {
    return SCacheCommands(*this);
}

PCacheCommands Client::pcache() {
    return PCacheCommands(*this);
}

KeyedCacheCommands Client::tool_cache() {
    return KeyedCacheCommands(*this, "TOOL.CACHE");
}

KeyedCacheCommands Client::ncache() {
    return KeyedCacheCommands(*this, "NCACHE");
}

KeyedCacheCommands Client::guard() {
    return KeyedCacheCommands(*this, "GUARD");
}

KeyedCacheCommands Client::rerank() {
    return KeyedCacheCommands(*this, "RERANK");
}

LlmStatsCommands Client::llmstats() {
    return LlmStatsCommands(*this);
}

LlmBudgetCommands Client::llmbudget() {
    return LlmBudgetCommands(*this);
}

PromptRegistryCommands Client::prompt() {
    return PromptRegistryCommands(*this);
}

EmbedDedupCommands Client::embed_dedup() {
    return EmbedDedupCommands(*this);
}

AgentMemoryCommands Client::amem() {
    return AgentMemoryCommands(*this);
}

RagCommands Client::rag() {
    return RagCommands(*this);
}

RateLimitCommands Client::ratelimit() {
    return RateLimitCommands(*this);
}

TimeSeriesCommands Client::ts() {
    return TimeSeriesCommands(*this);
}

FeatureStoreCommands Client::feature() {
    return FeatureStoreCommands(*this);
}

ObserveCommands Client::observe() {
    return ObserveCommands(*this);
}

AlertsCommands Client::alerts() {
    return AlertsCommands(*this);
}

} // namespace hotkv
