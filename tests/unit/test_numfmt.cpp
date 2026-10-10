// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/detail/numfmt.hpp>
#include <hotkv/hotkv.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

#ifndef _WIN32
#include "fake_server.hpp"
#endif

using namespace hotkv::detail;
using hotkv::InvalidArgumentError;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

// numpunct that would turn 1.5 into "1,5" and 1000 into "1.000" if any
// formatting path consulted the global C++ locale.
struct CommaPunct : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};

// Installs a hostile global locale (C++ and, when available, C) for one scope.
class HostileLocale {
public:
    HostileLocale() : old_cxx_(std::locale::global(std::locale(std::locale::classic(), new CommaPunct))) {
        const char* old = std::setlocale(LC_ALL, nullptr);
        old_c_ = old != nullptr ? old : "C";
        for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "German_Germany.1252"}) {
            if (std::setlocale(LC_ALL, name) != nullptr) {
                c_locale_installed_ = true;
                break;
            }
        }
    }
    ~HostileLocale() {
        std::setlocale(LC_ALL, old_c_.c_str());
        std::locale::global(old_cxx_);
    }
    bool c_locale_installed() const { return c_locale_installed_; }

private:
    std::locale old_cxx_;
    std::string old_c_;
    bool c_locale_installed_ = false;
};

} // namespace

TEST(NumFmtGeneral, ShortestRoundTrip) {
    EXPECT_EQ(format_double(0.0), "0");
    EXPECT_EQ(format_double(-0.0), "-0");
    EXPECT_EQ(format_double(1.0), "1");
    EXPECT_EQ(format_double(-2.5), "-2.5");
    EXPECT_EQ(format_double(0.1), "0.1");
    EXPECT_EQ(format_double(100.0), "100");
    EXPECT_EQ(format_double(123456789.125), "123456789.125");
    EXPECT_EQ(format_double(0.30000000000000004), "0.30000000000000004");
}

TEST(NumFmtGeneral, TinyAndHugeValuesRoundTripExactly) {
    for (double v : {1e-5, 1e-300, 5e-324, 1e21, 1e300, -1e-5, -1e300, 1.7976931348623157e308,
                     std::numeric_limits<double>::min()}) {
        std::string text = format_double(v);
        double back = 0;
        ASSERT_TRUE(parse_double(text, back)) << text;
        EXPECT_EQ(back, v) << text;
        EXPECT_EQ(text.find(','), std::string::npos) << text;
    }
    // The exponent forms are what to_chars picks when shorter; both are
    // accepted by the server's strtod / from_chars parsers.
    EXPECT_EQ(format_double(1e-5), "1e-05");
    EXPECT_EQ(format_double(1e21), "1e+21");
}

TEST(NumFmtGeneral, InfinityAndNan) {
    EXPECT_EQ(format_double(kInf), "inf");
    EXPECT_EQ(format_double(-kInf), "-inf");
    EXPECT_THROW(format_double(kNan), InvalidArgumentError);
    EXPECT_EQ(format_lossless(kNan), "nan");
    EXPECT_EQ(format_lossless(1.5), "1.5");
}

TEST(NumFmtFinite, RejectsNonFinite) {
    EXPECT_EQ(format_finite(0.5), "0.5");
    EXPECT_THROW(format_finite(kInf), InvalidArgumentError);
    EXPECT_THROW(format_finite(-kInf), InvalidArgumentError);
    EXPECT_THROW(format_finite(kNan), InvalidArgumentError);
}

TEST(NumFmtNonNegative, RejectsNegativeAndNonFinite) {
    EXPECT_EQ(format_nonnegative(0.25), "0.25");
    EXPECT_EQ(format_nonnegative(0.0), "0");
    EXPECT_EQ(format_nonnegative(-0.0), "0");
    EXPECT_THROW(format_nonnegative(-0.001), InvalidArgumentError);
    EXPECT_THROW(format_nonnegative(kInf), InvalidArgumentError);
    EXPECT_THROW(format_nonnegative(kNan), InvalidArgumentError);
}

// The server's LLMBUDGET.SET / LLMSTATS.PRICE parser accepts only
// [0-9]+(.[0-9]{1,6})? and the micro-dollar value must fit in 64 bits.
static bool server_accepts_usd(const std::string& s) {
    if (s.empty() || s[0] == '-' || s[0] == '+') {
        return false;
    }
    auto dot = s.find('.');
    std::string whole = dot == std::string::npos ? s : s.substr(0, dot);
    std::string frac = dot == std::string::npos ? "" : s.substr(dot + 1);
    if (whole.empty() || (dot != std::string::npos && frac.empty()) || frac.size() > 6) {
        return false;
    }
    for (char c : whole + frac) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

TEST(NumFmtUsd, FixedPointTrimmedNeverExponent) {
    EXPECT_EQ(format_usd(0.0), "0");
    EXPECT_EQ(format_usd(-0.0), "0");
    EXPECT_EQ(format_usd(5.0), "5");
    EXPECT_EQ(format_usd(0.5), "0.5");
    EXPECT_EQ(format_usd(1.234567), "1.234567");
    EXPECT_EQ(format_usd(3.0), "3");
    EXPECT_EQ(format_usd(0.000001), "0.000001");
    EXPECT_EQ(format_usd(12.30), "12.3");
    EXPECT_EQ(format_usd(1e-5), "0.00001");
    EXPECT_EQ(format_usd(1234567.0), "1234567");
}

TEST(NumFmtUsd, RoundsToNearestMicroDollar) {
    EXPECT_EQ(format_usd(1.2345678), "1.234568");
    EXPECT_EQ(format_usd(0.0000004), "0");
    EXPECT_EQ(format_usd(0.0000006), "0.000001");
    EXPECT_EQ(format_usd(2.9999999), "3");
    // Below half a micro-dollar vanishes entirely, tiny exponent inputs included.
    EXPECT_EQ(format_usd(1e-300), "0");
    EXPECT_EQ(format_usd(1e-7), "0");
}

TEST(NumFmtUsd, EveryOutputIsAcceptedByTheServerGrammar) {
    for (double v : {0.0, 1e-300, 1e-7, 1e-5, 0.1, 0.5, 1.0, 3.0, 15.0, 123.456789, 1e6, 99999999.999999,
                     18446744073709.0}) {
        std::string s = format_usd(v);
        EXPECT_TRUE(server_accepts_usd(s)) << v << " -> " << s;
        EXPECT_EQ(s.find('e'), std::string::npos) << s;
    }
}

TEST(NumFmtUsd, RejectsWhatTheServerWouldReject) {
    EXPECT_THROW(format_usd(-0.01), InvalidArgumentError);
    EXPECT_THROW(format_usd(-1e-300), InvalidArgumentError);
    EXPECT_THROW(format_usd(kInf), InvalidArgumentError);
    EXPECT_THROW(format_usd(-kInf), InvalidArgumentError);
    EXPECT_THROW(format_usd(kNan), InvalidArgumentError);
    // 1e21 and 1e300 overflow the server's 64 bit micro-dollar value.
    EXPECT_THROW(format_usd(1e21), InvalidArgumentError);
    EXPECT_THROW(format_usd(1e300), InvalidArgumentError);
    EXPECT_THROW(format_usd(18446744073710.0), InvalidArgumentError);
}

TEST(NumFmtParse, AcceptsWhatServersSend) {
    double v = 0;
    EXPECT_TRUE(parse_double("1.5", v));
    EXPECT_EQ(v, 1.5);
    EXPECT_TRUE(parse_double("+2.5", v));
    EXPECT_EQ(v, 2.5);
    EXPECT_TRUE(parse_double("-3e2", v));
    EXPECT_EQ(v, -300.0);
    EXPECT_TRUE(parse_double("1e-05", v));
    EXPECT_EQ(v, 1e-5);
    EXPECT_TRUE(parse_double("inf", v));
    EXPECT_EQ(v, kInf);
    EXPECT_TRUE(parse_double("-inf", v));
    EXPECT_EQ(v, -kInf);
    EXPECT_TRUE(parse_double("+inf", v));
    EXPECT_EQ(v, kInf);
    EXPECT_TRUE(parse_double("Infinity", v));
    EXPECT_TRUE(parse_double("nan", v));
    EXPECT_TRUE(std::isnan(v));
}

TEST(NumFmtParse, RejectsGarbage) {
    double v = 0;
    for (const char* bad : {"", "+", "-", "--1", "+-1", "1.5x", " 1", "1 ", "1,5", "abc", "0x10"}) {
        EXPECT_FALSE(parse_double(bad, v)) << bad;
    }
}

TEST(NumFmtLocale, NothingConsultsTheGlobalLocale) {
    HostileLocale hostile;
    // Sanity: the hostile C++ locale really does break ostream formatting.
    std::ostringstream probe;
    probe << 1.5;
    ASSERT_EQ(probe.str(), "1,5");

    EXPECT_EQ(format_double(1.5), "1.5");
    EXPECT_EQ(format_double(1234567.891), "1234567.891");
    EXPECT_EQ(format_finite(-0.25), "-0.25");
    EXPECT_EQ(format_nonnegative(1000.5), "1000.5");
    EXPECT_EQ(format_usd(1234.5), "1234.5");
    EXPECT_EQ(format_usd(0.000001), "0.000001");

    double v = 0;
    EXPECT_TRUE(parse_double("1234.5", v));
    EXPECT_EQ(v, 1234.5);
    EXPECT_FALSE(parse_double("1234,5", v));

    // Reply-side coercion goes through the same parser.
    EXPECT_EQ(hotkv::Reply::make_bulk_string("2.75").as_double(), 2.75);

    if (!hostile.c_locale_installed()) {
        GTEST_LOG_(INFO) << "no de_DE/fr_FR C locale installed here; only the C++ comma locale was exercised";
    }
}

TEST(NumFmtLocale, ExecuteArgumentsUseTheSameFormatter) {
    HostileLocale hostile;
    // Client::execute() renders doubles through to_arg(); with the old
    // std::to_string(double) this was "0.500000" (6 decimals) and locale
    // dependent.
    EXPECT_EQ(to_arg(0.5), "0.5");
    EXPECT_EQ(to_arg(1e-5), "1e-05");
    EXPECT_EQ(to_arg(2.5f), "2.5");
    EXPECT_EQ(to_arg(42), "42");
    EXPECT_THROW(to_arg(kNan), InvalidArgumentError);
}

#ifndef _WIN32

// Exact wire arguments of every typed method that sends a double, under a
// hostile locale. The fake server answers every command with a generic
// success so only the request is checked.
namespace {

struct WireProbe {
    hotkv::testutil::FakeServer server{[](int, const std::vector<std::string>& argv) -> std::string {
        if (argv[0] == "HELLO") {
            return "-ERR unknown command\r\n";
        }
        if (argv[0] == "INCRBYFLOAT" || argv[0] == "HINCRBYFLOAT" || argv[0] == "ZINCRBY") {
            return "$3\r\n1.5\r\n";
        }
        if (argv[0] == "GEOSEARCH") {
            return "*0\r\n";
        }
        if (argv[0] == "LLMBUDGET.CONSUME" || argv[0] == "ZADD" || argv[0] == "GEOADD" ||
            argv[0] == "ZUNIONSTORE" || argv[0] == "VSIM.SET" || argv[0] == "TS.ADD") {
            return ":1\r\n";
        }
        return "+OK\r\n";
    }};
    hotkv::Client client = [&] {
        hotkv::Options o;
        o.port = server.port();
        o.pool_size = 1;
        return hotkv::Client(o);
    }();

    std::vector<std::string> last(const std::string& name) {
        auto cmds = server.commands_named(name);
        EXPECT_FALSE(cmds.empty()) << name;
        return cmds.empty() ? std::vector<std::string>{} : cmds.back().argv;
    }
};

using V = std::vector<std::string>;

} // namespace

TEST(NumFmtWire, CoreCommandsSendLocaleFreeShortestForms) {
    HostileLocale hostile;
    WireProbe p;
    p.client.incrbyfloat("k", 0.1);
    EXPECT_EQ(p.last("INCRBYFLOAT"), (V{"INCRBYFLOAT", "k", "0.1"}));
    p.client.incrbyfloat("k", -1e-5);
    EXPECT_EQ(p.last("INCRBYFLOAT"), (V{"INCRBYFLOAT", "k", "-1e-05"}));
    p.client.hincrbyfloat("h", "f", 1234.5);
    EXPECT_EQ(p.last("HINCRBYFLOAT"), (V{"HINCRBYFLOAT", "h", "f", "1234.5"}));
    p.client.zincrby("z", 2.25, "m");
    EXPECT_EQ(p.last("ZINCRBY"), (V{"ZINCRBY", "z", "2.25", "m"}));
    p.client.zadd("z", {{"a", 1.5}, {"b", -kInf}, {"c", 1e21}});
    EXPECT_EQ(p.last("ZADD"), (V{"ZADD", "z", "1.5", "a", "-inf", "b", "1e+21", "c"}));
    p.client.geoadd("g", {{13.361389, 38.115556, "Palermo"}});
    EXPECT_EQ(p.last("GEOADD"), (V{"GEOADD", "g", "13.361389", "38.115556", "Palermo"}));
    hotkv::GeoSearchOptions gs;
    gs.from_lonlat = hotkv::GeoPoint{15.0, 37.5};
    gs.radius = 200.5;
    gs.unit = hotkv::GeoUnit::Kilometers;
    p.client.geosearch("g", gs);
    EXPECT_EQ(p.last("GEOSEARCH"), (V{"GEOSEARCH", "g", "FROMLONLAT", "15", "37.5", "BYRADIUS", "200.5", "km"}));
    hotkv::ZStoreOptions zs;
    zs.weights = {0.5, 2};
    p.client.zunionstore("d", {"a", "b"}, zs);
    EXPECT_EQ(p.last("ZUNIONSTORE"), (V{"ZUNIONSTORE", "d", "2", "a", "b", "WEIGHTS", "0.5", "2"}));
}

TEST(NumFmtWire, NonFiniteArgumentsThrowBeforeAnythingIsSent) {
    WireProbe p;
    EXPECT_THROW(p.client.incrbyfloat("k", kNan), InvalidArgumentError);
    EXPECT_THROW(p.client.incrbyfloat("k", kInf), InvalidArgumentError);
    EXPECT_THROW(p.client.hincrbyfloat("h", "f", -kInf), InvalidArgumentError);
    EXPECT_THROW(p.client.zincrby("z", kNan, "m"), InvalidArgumentError);
    EXPECT_THROW(p.client.geoadd("g", {{kInf, 0.0, "x"}}), InvalidArgumentError);
    EXPECT_TRUE(p.server.commands().empty());
}

TEST(NumFmtWire, UsdArgumentsAreFixedPointUnderAnyLocale) {
    HostileLocale hostile;
    WireProbe p;
    p.client.llmbudget().set("b", 1000, 12.5, 60);
    EXPECT_EQ(p.last("LLMBUDGET.SET"), (V{"LLMBUDGET.SET", "b", "1000", "12.5", "60"}));
    p.client.llmbudget().set("b", 1000, 0.000001, 60);
    EXPECT_EQ(p.last("LLMBUDGET.SET"), (V{"LLMBUDGET.SET", "b", "1000", "0.000001", "60"}));
    p.client.llmbudget().set("b", 1000, 1e-5, 60);
    EXPECT_EQ(p.last("LLMBUDGET.SET"), (V{"LLMBUDGET.SET", "b", "1000", "0.00001", "60"}));
    p.client.llmbudget().consume("b", 10, 0.0123456789);
    EXPECT_EQ(p.last("LLMBUDGET.CONSUME"), (V{"LLMBUDGET.CONSUME", "b", "10", "0.012346"}));
    EXPECT_THROW(p.client.llmbudget().set("b", 1000, -1.0, 60), InvalidArgumentError);
    EXPECT_THROW(p.client.llmbudget().set("b", 1000, 1e21, 60), InvalidArgumentError);
    EXPECT_THROW(p.client.llmbudget().consume("b", 10, kNan), InvalidArgumentError);
    EXPECT_THROW(p.client.llmbudget().consume("b", 10, -0.5), InvalidArgumentError);

    p.client.llmstats().record("bk", 1, 2, 3, 0.5, 40);
    EXPECT_EQ(p.last("LLMSTATS.RECORD"), (V{"LLMSTATS.RECORD", "bk", "1", "2", "3", "0.5", "40"}));
    EXPECT_THROW(p.client.llmstats().record("bk", 1, 2, 3, -0.5, 40), InvalidArgumentError);
}

TEST(NumFmtWire, LlmStatsPriceAmountsAreFixedPoint) {
    HostileLocale hostile;
    WireProbe p;
    p.client.llmstats().price_set("m", 3.0, 15.0);
    auto a = p.last("LLMSTATS.PRICE");
    ASSERT_GE(a.size(), 7u);
    // Every amount token must satisfy the server's fixed-point grammar.
    for (std::size_t i = 4; i < a.size(); i += 2) {
        EXPECT_TRUE(server_accepts_usd(a[i])) << a[i];
    }
    hotkv::LlmPriceOptions o;
    o.cache_read_usd_per_mtok = 0.3;
    o.cache_write_5m_usd_per_mtok = 3.75;
    p.client.llmstats().price_set("m", 3.0, 15.0, o);
    auto b = p.last("LLMSTATS.PRICE");
    for (std::size_t i = 4; i < b.size(); i += 2) {
        EXPECT_TRUE(server_accepts_usd(b[i])) << b[i];
    }
    EXPECT_THROW(p.client.llmstats().price_set("m", -1.0, 15.0), InvalidArgumentError);
    EXPECT_THROW(p.client.llmstats().price_set("m", kNan, 15.0), InvalidArgumentError);
}

TEST(NumFmtWire, VectorsThresholdsAndRatesAreFiniteLocaleFree) {
    HostileLocale hostile;
    WireProbe p;
    p.client.vsim().set("idx", "k", {0.5, -1.25, 1e-7});
    auto vs = p.last("VSIM.SET");
    EXPECT_EQ(vs, (V{"VSIM.SET", "idx", "k", "0.5", "-1.25", "1e-07"}));
    EXPECT_THROW(p.client.vsim().set("idx", "k", {0.5, kNan}), InvalidArgumentError);
    EXPECT_THROW(p.client.vsim().set("idx", "k", {kInf}), InvalidArgumentError);

    p.client.scache().set_threshold("ns", 0.85);
    EXPECT_EQ(p.last("SCACHE.THRESHOLD"), (V{"SCACHE.THRESHOLD", "ns", "0.85"}));

    p.client.pcache().config_set_cost_per_hit("ns", 0.0025);
    EXPECT_EQ(p.last("PCACHE.CONFIG"), (V{"PCACHE.CONFIG", "ns", "SET", "COST_PER_HIT", "0.0025"}));
    EXPECT_THROW(p.client.pcache().config_set_cost_per_hit("ns", -1.0), InvalidArgumentError);

    p.client.ts().add("series", 1000, 21.5);
    EXPECT_EQ(p.last("TS.ADD"), (V{"TS.ADD", "series", "1000", "21.5"}));
    p.client.ts().add("series", std::nullopt, -0.125);
    EXPECT_EQ(p.last("TS.ADD"), (V{"TS.ADD", "series", "*", "-0.125"}));
    EXPECT_THROW(p.client.ts().add("series", 1, kNan), InvalidArgumentError);

    hotkv::RateLimitCreateOptions rl;
    rl.refill = 2.5;
    p.client.ratelimit().create("rl", 10, 1000, rl);
    auto r = p.last("RATELIMIT.CREATE");
    EXPECT_NE(std::find(r.begin(), r.end(), "2.5"), r.end());
}

#endif // !_WIN32
