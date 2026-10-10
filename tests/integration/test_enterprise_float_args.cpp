// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

// Live checks that the floating point arguments the SDK formats for the
// enterprise commands are accepted by a real (licensed) server and are
// stored with full micro-dollar precision. The server's LLMBUDGET.SET and
// LLMSTATS.PRICE parsers only take fixed-point text, so exponent forms
// ("1e-05") or std::to_string-style padding would be rejected or lose
// precision; these tests would fail against such output.

#include "test_helpers.hpp"

#include <cmath>
#include <limits>

using hotkv::testutil::IntegrationTest;

TEST_F(IntegrationTest, LlmBudgetLimitsKeepMicroDollarPrecision) {
    std::string name = key("budget-float");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.llmbudget().set(name, 1000000, 12.5, 3600), "llm-budget");
    EXPECT_NEAR(client_.llmbudget().status(name).cost_limit_usd, 12.5, 1e-9);

    // Smallest representable limit: one micro-dollar.
    client_.llmbudget().set(name, 1000000, 0.000001, 3600);
    EXPECT_NEAR(client_.llmbudget().status(name).cost_limit_usd, 0.000001, 1e-12);

    // A double whose shortest form is an exponent ("1e-05") must still
    // reach the server as plain fixed point.
    client_.llmbudget().set(name, 1000000, 1e-5, 3600);
    EXPECT_NEAR(client_.llmbudget().status(name).cost_limit_usd, 0.00001, 1e-12);

    client_.llmbudget().set(name, 1000000, 1234.567891, 3600);
    EXPECT_NEAR(client_.llmbudget().status(name).cost_limit_usd, 1234.567891, 1e-9);

    // Sub-micro-dollar input rounds to the nearest micro-dollar.
    client_.llmbudget().set(name, 1000000, 0.1234567, 3600);
    EXPECT_NEAR(client_.llmbudget().status(name).cost_limit_usd, 0.123457, 1e-9);

    client_.llmbudget().set(name, 1000000, 0.0, 3600);
    EXPECT_DOUBLE_EQ(client_.llmbudget().status(name).cost_limit_usd, 0.0);

    // Values the server could never accept are refused before sending.
    EXPECT_THROW(client_.llmbudget().set(name, 1, -1.0, 60), hotkv::InvalidArgumentError);
    EXPECT_THROW(client_.llmbudget().set(name, 1, std::numeric_limits<double>::infinity(), 60),
                 hotkv::InvalidArgumentError);
    EXPECT_THROW(client_.llmbudget().set(name, 1, std::nan(""), 60), hotkv::InvalidArgumentError);
    EXPECT_THROW(client_.llmbudget().set(name, 1, 1e21, 60), hotkv::InvalidArgumentError);
    client_.llmbudget().drop(name);
}

TEST_F(IntegrationTest, LlmBudgetConsumeAccountsTinyAndRoundedCosts) {
    std::string name = key("budget-consume");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.llmbudget().set(name, 1000000, 10.0, 3600), "llm-budget");
    EXPECT_TRUE(client_.llmbudget().consume(name, 1, 0.000001));
    EXPECT_TRUE(client_.llmbudget().consume(name, 1, 2.5));
    EXPECT_TRUE(client_.llmbudget().consume(name, 1, 1e-5));
    auto status = client_.llmbudget().status(name);
    EXPECT_NEAR(status.cost_used_usd, 2.500011, 1e-6);
    EXPECT_EQ(status.tokens_used, 3);

    // Over the cap: refused (false), not an error.
    EXPECT_FALSE(client_.llmbudget().consume(name, 1, 20.0));
    EXPECT_THROW(client_.llmbudget().consume(name, 1, -1.0), hotkv::InvalidArgumentError);
    client_.llmbudget().drop(name);
}

TEST_F(IntegrationTest, LlmStatsBucketRecordTinyAndFractionalCosts) {
    std::string bucket = key("stats-float");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.llmstats().bucket(bucket), "llm-stats");
    client_.llmstats().record(bucket, 10, 5, 0, 0.5, 100);
    client_.llmstats().record(bucket, 10, 5, 0, 1e-5, 100);
    client_.llmstats().record(bucket, 10, 5, 0, 0.000001, 100);
    auto snap = client_.llmstats().get(bucket);
    EXPECT_EQ(snap.requests, 3);
    EXPECT_NEAR(snap.cost_usd, 0.500011, 1e-6);
    EXPECT_THROW(client_.llmstats().record(bucket, 1, 1, 0, -0.1, 1), hotkv::InvalidArgumentError);
    EXPECT_THROW(client_.llmstats().record(bucket, 1, 1, 0, std::nan(""), 1), hotkv::InvalidArgumentError);
    EXPECT_TRUE(client_.llmstats().drop(bucket));
}

TEST_F(IntegrationTest, LlmStatsPricesKeepMicroDollarPrecision) {
    std::string model = key("price-float");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.llmstats().price_set(model, 0.000001, 1e-5), "llm-stats");
    auto price = client_.llmstats().price_get(model);
    EXPECT_TRUE(price.priced);
    EXPECT_NEAR(price.input_usd_per_mtok, 0.000001, 1e-12);
    EXPECT_NEAR(price.output_usd_per_mtok, 0.00001, 1e-12);

    hotkv::LlmPriceOptions overrides;
    overrides.cache_read_usd_per_mtok = 0.3;
    overrides.cache_write_5m_usd_per_mtok = 3.75;
    overrides.cache_write_1h_usd_per_mtok = 6.0;
    client_.llmstats().price_set(model, 3.0, 15.0, overrides);
    price = client_.llmstats().price_get(model);
    EXPECT_NEAR(price.input_usd_per_mtok, 3.0, 1e-9);
    EXPECT_NEAR(price.output_usd_per_mtok, 15.0, 1e-9);
    EXPECT_NEAR(price.cache_read_usd_per_mtok, 0.3, 1e-9);
    EXPECT_NEAR(price.cache_write_5m_usd_per_mtok, 3.75, 1e-9);
    EXPECT_NEAR(price.cache_write_1h_usd_per_mtok, 6.0, 1e-9);

    EXPECT_THROW(client_.llmstats().price_set(model, -1.0, 15.0), hotkv::InvalidArgumentError);
    EXPECT_TRUE(client_.llmstats().price_del(model));
}

TEST_F(IntegrationTest, LlmStatsModelPricedRecordUsesTheStoredPrice) {
    std::string model = key("price-record");
    std::string bucket = key("price-record-bucket");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.llmstats().price_set(model, 2.0, 10.0), "llm-stats");
    hotkv::LlmStatsRecordOptions opts;
    opts.model = model;
    // 1,000,000 input tokens at $2/Mtok + 500,000 output at $10/Mtok = $7.
    client_.llmstats().record(bucket, 1000000, 500000, 0, 0.0, 10, opts);
    EXPECT_NEAR(client_.llmstats().get(bucket).cost_usd, 7.0, 1e-6);
    client_.llmstats().drop(bucket);
    client_.llmstats().price_del(model);
}

TEST_F(IntegrationTest, PCacheCostPerHitAcceptsSmallValues) {
    std::string ns = key("pcache-cost");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.pcache().create(ns), "prompt-cache");
    client_.pcache().config_set_cost_per_hit(ns, 0.0025);
    client_.pcache().config_set_cost_per_hit(ns, 1e-7);
    EXPECT_THROW(client_.pcache().config_set_cost_per_hit(ns, -0.5), hotkv::InvalidArgumentError);
    EXPECT_TRUE(client_.pcache().drop(ns));
}

TEST_F(IntegrationTest, TimeSeriesAcceptsTinyHugeAndNegativeValues) {
    std::string ts = key("ts-float");
    HOTKV_SKIP_IF_NOT_LICENSED(client_.ts().create(ts), "time-series");
    client_.ts().add(ts, 1000, 1e-300);
    client_.ts().add(ts, 2000, -2.5);
    client_.ts().add(ts, 3000, 1e300);
    client_.ts().add(ts, 4000, 0.1);
    auto samples = client_.ts().range(ts, "-", "+");
    ASSERT_EQ(samples.size(), 4u);
    // The server prints sample values with a fixed 6 decimals, so the
    // tiny one reads back as 0 and the huge one as its full decimal
    // expansion; both were accepted on the way in, which is what this
    // checks (a rejected argument would have thrown above).
    EXPECT_NEAR(samples[0].value, 0.0, 1e-6);
    EXPECT_EQ(samples[1].value, -2.5);
    EXPECT_NEAR(samples[2].value / 1e300, 1.0, 1e-12);
    EXPECT_EQ(samples[3].value, 0.1);
    client_.ts().del(ts);
}

TEST_F(IntegrationTest, RateLimitFractionalRates) {
    std::string name = key("rl-float");
    hotkv::RateLimitCreateOptions opts;
    opts.algo = "token_bucket";
    opts.refill = 0.5;
    HOTKV_SKIP_IF_NOT_LICENSED(client_.ratelimit().create(name, 10, 1000, opts), "rate-limiting");
    auto r = client_.ratelimit().check(name, "user1");
    EXPECT_TRUE(r.allowed);
    client_.ratelimit().drop(name);
}

TEST_F(IntegrationTest, VectorComponentsOfAnyMagnitudeRoundTrip) {
    std::string index = key("vsim-float");
    // VSIM index names are plain tokens; the colon-separated test key is fine.
    HOTKV_SKIP_IF_NOT_LICENSED(client_.vsim().create_index(index, 4), "vsim");
    std::vector<double> v{0.1, -1e-7, 123.456, 1e-30};
    client_.vsim().set(index, "k1", v);
    auto back = client_.vsim().get(index, "k1");
    ASSERT_TRUE(back.has_value());
    ASSERT_EQ(back->size(), 4u);
    // The server stores L2-normalised float32 vectors, so compare against
    // the normalised input.
    double norm = 0;
    for (double x : v) {
        norm += x * x;
    }
    norm = std::sqrt(norm);
    for (std::size_t i = 0; i < v.size(); ++i) {
        double want = v[i] / norm;
        EXPECT_NEAR((*back)[i], want, std::abs(want) * 1e-5 + 1e-37) << i;
    }
    EXPECT_THROW(client_.vsim().set(index, "k2", {0.1, std::nan(""), 0.0, 0.0}), hotkv::InvalidArgumentError);
    client_.vsim().drop_index(index);
}
