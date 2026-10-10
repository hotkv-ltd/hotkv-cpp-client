// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>


namespace hotkv {

namespace {

void append_vector(std::vector<std::string>& args, const std::vector<double>& vector) {
    for (double v : vector) {
        args.push_back(detail::format_finite(v));
    }
}

} // namespace

void VsimCommands::create_index(std::string_view name, int64_t dims, const VsimCreateOptions& opts) {
    std::vector<std::string> args{"VSIM.CREATEINDEX", std::string(name), std::to_string(dims)};
    if (!opts.metric.empty()) {
        args.emplace_back("METRIC");
        args.push_back(opts.metric);
    }
    if (opts.m.has_value()) {
        args.emplace_back("M");
        args.push_back(std::to_string(*opts.m));
    }
    if (opts.ef_construction.has_value()) {
        args.emplace_back("EF_CONSTRUCTION");
        args.push_back(std::to_string(*opts.ef_construction));
    }
    if (opts.ef_search.has_value()) {
        args.emplace_back("EF_SEARCH");
        args.push_back(std::to_string(*opts.ef_search));
    }
    client_.command(std::move(args));
}

void VsimCommands::drop_index(std::string_view name) {
    client_.command({"VSIM.DROPINDEX", std::string(name)});
}

bool VsimCommands::set(std::string_view index, std::string_view key, const std::vector<double>& vector) {
    std::vector<std::string> args{"VSIM.SET", std::string(index), std::string(key)};
    append_vector(args, vector);
    return client_.command(std::move(args)).as_int() != 0;
}

std::optional<std::vector<double>> VsimCommands::get(std::string_view index, std::string_view key) {
    Reply r = client_.command({"VSIM.GET", std::string(index), std::string(key)});
    if (r.is_nil()) {
        return std::nullopt;
    }
    std::vector<double> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_double());
    }
    return out;
}

bool VsimCommands::del(std::string_view index, std::string_view key) {
    return client_.command({"VSIM.DEL", std::string(index), std::string(key)}).as_int() != 0;
}

std::vector<VsimSearchResult> VsimCommands::search(std::string_view index, int64_t k,
                                                     const std::vector<double>& vector,
                                                     const std::vector<std::string>& filter_tags,
                                                     std::optional<int64_t> ef) {
    std::vector<std::string> args{"VSIM.SEARCH", std::string(index), std::to_string(k)};
    append_vector(args, vector);
    if (!filter_tags.empty()) {
        args.emplace_back("FILTER");
        args.insert(args.end(), filter_tags.begin(), filter_tags.end());
    }
    if (ef.has_value()) {
        args.emplace_back("EF");
        args.push_back(std::to_string(*ef));
    }
    Reply r = client_.command(std::move(args));
    std::vector<VsimSearchResult> out;
    // Documented as a flat alternating array, but decode the RESP3
    // array-of-pairs shape too in case the server upgrades it, the way
    // it does for ZRANGE WITHSCORES and friends.
    for (auto& pair : r.as_pair_list()) {
        out.push_back(VsimSearchResult{pair.first.as_string(), pair.second.as_double()});
    }
    return out;
}

std::vector<std::string> VsimCommands::list_indexes() {
    return client_.command({"VSIM.INFO"}).as_string_array();
}

VsimIndexInfo VsimCommands::info(std::string_view index) {
    Reply r = client_.command({"VSIM.INFO", std::string(index)});
    VsimIndexInfo out;
    for (auto& kv : r.as_pairs()) {
        if (kv.first == "name") {
            out.name = kv.second.as_string();
        } else if (kv.first == "dimensions") {
            out.dimensions = kv.second.as_int();
        } else if (kv.first == "metric") {
            out.metric = kv.second.as_string();
        } else if (kv.first == "vectors") {
            out.vectors = kv.second.as_int();
        } else if (kv.first == "M") {
            out.m = kv.second.as_int();
        } else if (kv.first == "ef_construction") {
            out.ef_construction = kv.second.as_int();
        } else if (kv.first == "ef_search") {
            out.ef_search = kv.second.as_int();
        }
    }
    return out;
}

int64_t VsimCommands::save(std::string_view snapshot_name, const std::string& blob) {
    return client_.command({"VSIM.SAVE", std::string(snapshot_name), blob}).as_int();
}

std::optional<std::string> VsimCommands::load(std::string_view snapshot_name) {
    return client_.command({"VSIM.LOAD", std::string(snapshot_name)}).as_optional_string();
}

std::vector<std::string> VsimCommands::list_snapshots() {
    return client_.command({"VSIM.LIST"}).as_string_array();
}

bool VsimCommands::drop_snapshot(std::string_view snapshot_name) {
    return client_.command({"VSIM.DROP", std::string(snapshot_name)}).as_int() != 0;
}

} // namespace hotkv
