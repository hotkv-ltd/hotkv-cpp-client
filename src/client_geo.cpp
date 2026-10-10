// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/client.hpp>

namespace hotkv {

namespace {

const char* unit_name(GeoUnit unit) {
    switch (unit) {
        case GeoUnit::Meters:
            return "m";
        case GeoUnit::Kilometers:
            return "km";
        case GeoUnit::Miles:
            return "mi";
        case GeoUnit::Feet:
            return "ft";
    }
    return "m";
}

void append_geosearch_options(std::vector<std::string>& args, const GeoSearchOptions& opts) {
    if (opts.from_member.has_value()) {
        args.emplace_back("FROMMEMBER");
        args.push_back(*opts.from_member);
    } else if (opts.from_lonlat.has_value()) {
        args.emplace_back("FROMLONLAT");
        args.push_back(detail::format_finite(opts.from_lonlat->longitude));
        args.push_back(detail::format_finite(opts.from_lonlat->latitude));
    }
    if (opts.radius.has_value()) {
        args.emplace_back("BYRADIUS");
        args.push_back(detail::format_finite(*opts.radius));
        args.emplace_back(unit_name(opts.unit));
    } else if (opts.box.has_value()) {
        args.emplace_back("BYBOX");
        args.push_back(detail::format_finite(opts.box->first));
        args.push_back(detail::format_finite(opts.box->second));
        args.emplace_back(unit_name(opts.unit));
    }
    if (opts.asc) {
        args.emplace_back("ASC");
    } else if (opts.desc) {
        args.emplace_back("DESC");
    }
    if (opts.count.has_value()) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(*opts.count));
        if (opts.count_any) {
            args.emplace_back("ANY");
        }
    }
    if (opts.with_coord) {
        args.emplace_back("WITHCOORD");
    }
    if (opts.with_dist) {
        args.emplace_back("WITHDIST");
    }
    if (opts.with_hash) {
        args.emplace_back("WITHHASH");
    }
}

} // namespace

int64_t Client::geoadd(std::string_view key, const std::vector<std::tuple<double, double, std::string>>& points) {
    std::vector<std::string> args{"GEOADD", std::string(key)};
    for (const auto& p : points) {
        args.push_back(detail::format_finite(std::get<0>(p)));
        args.push_back(detail::format_finite(std::get<1>(p)));
        args.push_back(std::get<2>(p));
    }
    return command(std::move(args)).as_int();
}

std::optional<double> Client::geodist(std::string_view key, std::string_view member1, std::string_view member2,
                                       GeoUnit unit) {
    Reply r = command({"GEODIST", std::string(key), std::string(member1), std::string(member2), unit_name(unit)});
    return r.as_optional_double();
}

std::vector<std::optional<GeoPoint>> Client::geopos(std::string_view key, const std::vector<std::string>& members) {
    std::vector<std::string> args{"GEOPOS", std::string(key)};
    args.insert(args.end(), members.begin(), members.end());
    Reply r = command(std::move(args));
    std::vector<std::optional<GeoPoint>> out;
    for (const Reply& item : r.as_array()) {
        if (item.is_nil()) {
            out.push_back(std::nullopt);
            continue;
        }
        const Reply::Array& pair = item.as_array();
        // Longitude/latitude may be a native RESP3 double or a bulk
        // string; as_double() coerces either.
        out.push_back(GeoPoint{pair[0].as_double(), pair[1].as_double()});
    }
    return out;
}

std::vector<std::optional<std::string>> Client::geohash(std::string_view key,
                                                          const std::vector<std::string>& members) {
    std::vector<std::string> args{"GEOHASH", std::string(key)};
    args.insert(args.end(), members.begin(), members.end());
    Reply r = command(std::move(args));
    std::vector<std::optional<std::string>> out;
    for (const Reply& item : r.as_array()) {
        out.push_back(item.as_optional_string());
    }
    return out;
}

std::vector<GeoSearchResult> Client::geosearch(std::string_view key, const GeoSearchOptions& opts) {
    std::vector<std::string> args{"GEOSEARCH", std::string(key)};
    append_geosearch_options(args, opts);
    Reply r = command(std::move(args));

    std::vector<GeoSearchResult> out;
    for (const Reply& item : r.as_array()) {
        GeoSearchResult res;
        if (!opts.with_coord && !opts.with_dist && !opts.with_hash) {
            // Plain member name, no extra fields requested.
            res.member = item.as_string();
            out.push_back(std::move(res));
            continue;
        }
        const Reply::Array& fields = item.as_array();
        std::size_t idx = 0;
        res.member = fields.at(idx++).as_string();
        if (opts.with_dist) {
            res.distance = fields.at(idx++).as_double();
        }
        if (opts.with_hash) {
            res.geohash = fields.at(idx++).as_int();
        }
        if (opts.with_coord) {
            const Reply::Array& coord = fields.at(idx++).as_array();
            res.coord = GeoPoint{coord[0].as_double(), coord[1].as_double()};
        }
        out.push_back(std::move(res));
    }
    return out;
}

int64_t Client::geosearchstore(std::string_view dst, std::string_view src, const GeoSearchOptions& opts) {
    std::vector<std::string> args{"GEOSEARCHSTORE", std::string(dst), std::string(src)};
    append_geosearch_options(args, opts);
    return command(std::move(args)).as_int();
}

} // namespace hotkv
