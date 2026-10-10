// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

#include "pairs_util.hpp"

namespace hotkv {

// --- ObserveCommands ---

ObserveInfo ObserveCommands::info() {
    auto pairs = client_.command({"OBSERVE", "INFO"}).as_pairs();
    ObserveInfo out;
    out.uptime_seconds = detail::pairs_int(pairs, "uptime_seconds");
    out.connected_clients = detail::pairs_int(pairs, "connected_clients");
    out.total_commands = detail::pairs_int(pairs, "total_commands");
    out.used_memory_bytes = detail::pairs_int(pairs, "used_memory_bytes");
    out.peak_memory_bytes = detail::pairs_int(pairs, "peak_memory_bytes");
    out.total_connections = detail::pairs_int(pairs, "total_connections");
    out.enterprise_features = detail::pairs_string(pairs, "enterprise_features");
    return out;
}

ObserveMetrics ObserveCommands::metrics() {
    auto pairs = client_.command({"OBSERVE", "METRICS"}).as_pairs();
    ObserveMetrics out;
    out.uptime_seconds = detail::pairs_int(pairs, "uptime_seconds");
    out.connected_clients = detail::pairs_int(pairs, "connected_clients");
    out.total_commands = detail::pairs_int(pairs, "total_commands");
    out.total_connections = detail::pairs_int(pairs, "total_connections");
    out.used_memory_bytes = detail::pairs_int(pairs, "used_memory_bytes");
    out.peak_memory_bytes = detail::pairs_int(pairs, "peak_memory_bytes");
    out.slowlog_length = detail::pairs_int(pairs, "slowlog_length");
    out.alerts_sent = detail::pairs_int(pairs, "alerts_sent");
    out.alerts_failed = detail::pairs_int(pairs, "alerts_failed");
    out.num_workers = detail::pairs_int(pairs, "num_workers");
    return out;
}

// --- AlertsCommands ---

AlertsInfo AlertsCommands::info() {
    auto pairs = client_.command({"ALERTS", "INFO"}).as_pairs();
    AlertsInfo out;
    out.enabled = detail::pairs_bool(pairs, "enabled");
    out.flag_enabled = detail::pairs_bool(pairs, "flag_enabled");
    out.webhook_url = detail::pairs_string(pairs, "webhook_url");
    out.provider = detail::pairs_string(pairs, "provider");
    out.supported_providers = detail::pairs_string(pairs, "supported_providers");
    out.min_severity = detail::pairs_string(pairs, "min_severity");
    out.alerts_sent = detail::pairs_int(pairs, "alerts_sent");
    out.alerts_failed = detail::pairs_int(pairs, "alerts_failed");
    out.alerts_dropped = detail::pairs_int(pairs, "alerts_dropped");
    out.alerts_rate_limited = detail::pairs_int(pairs, "alerts_rate_limited");
    out.alerts_deduped = detail::pairs_int(pairs, "alerts_deduped");
    out.alerts_filtered = detail::pairs_int(pairs, "alerts_filtered");
    out.alerts_queued = detail::pairs_int(pairs, "alerts_queued");
    return out;
}

void AlertsCommands::test(std::string_view severity, std::string_view message) {
    // The server reads args[2] strictly as the severity token when
    // present (rejecting anything that is not info/warning/critical), so
    // a custom message can only be sent alongside an explicit severity.
    std::vector<std::string> args{"ALERTS", "TEST"};
    if (!severity.empty()) {
        args.emplace_back(severity);
    }
    if (!message.empty()) {
        args.emplace_back(message);
    }
    client_.command(std::move(args));
}

void AlertsCommands::set_url(std::string_view url) {
    client_.command({"ALERTS", "SETURL", std::string(url)});
}

void AlertsCommands::set_severity(std::string_view severity) {
    client_.command({"ALERTS", "SETSEVERITY", std::string(severity)});
}

void AlertsCommands::on() {
    client_.command({"ALERTS", "ON"});
}

void AlertsCommands::off() {
    client_.command({"ALERTS", "OFF"});
}

} // namespace hotkv
