// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

// "Real user" test: connects to a public, TLS-protected, password-protected
// HotKV instance over the internet, the way a customer's application would,
// and exercises the SDK end to end. Skipped unless HOTKV_REMOTE_HOST is set.
//
//   HOTKV_REMOTE_HOST      host or IP of the instance
//   HOTKV_REMOTE_PORT      its TLS port
//   HOTKV_REMOTE_CA        PEM CA bundle that signed the server certificate
//                          (the certificate must name HOTKV_REMOTE_HOST)
//   HOTKV_REMOTE_PASSWORD  the instance password (environment only: it is
//                          never written to a file, a log or a message)
//
// The instance is shared, so every key, namespace, index and bucket lives
// under a unique "remote-cpp-<random>" prefix and is removed afterwards.
// Nothing global is touched: no FLUSHALL/FLUSHDB, CONFIG SET, ACL change,
// SHUTDOWN, HOTKEYS RESET or ALERTS change.

#include "test_helpers.hpp"

#include <hotkv/ai.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <random>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct RemoteEnv {
    std::string host;
    std::string port;
    std::string ca;
    std::string password;
    bool configured() const { return !host.empty() && !port.empty() && !password.empty(); }
};

RemoteEnv remote_env() {
    auto get = [](const char* n) {
        const char* v = std::getenv(n);
        return (v != nullptr) ? std::string(v) : std::string();
    };
    return RemoteEnv{get("HOTKV_REMOTE_HOST"), get("HOTKV_REMOTE_PORT"), get("HOTKV_REMOTE_CA"),
                     get("HOTKV_REMOTE_PASSWORD")};
}

hotkv::Options options_form(const RemoteEnv& e) {
    hotkv::Options o;
    o.host = e.host;
    o.port = static_cast<uint16_t>(std::stoi(e.port));
    o.password = e.password;
    o.tls.enabled = true;
    o.tls.ca_file = e.ca;
    o.connect_timeout = 10s;
    o.command_timeout = 15s;
    o.pool_size = 8;
    o.client_name = "hotkv-cpp-remote-test";
    return o;
}

std::string url_form(const RemoteEnv& e) {
    return "hotkvs://:" + e.password + "@" + e.host + ":" + e.port + "?tls_ca_file=" + e.ca +
           "&connect_timeout_ms=10000&command_timeout_ms=15000&pool_size=8";
}

std::string random_token() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(gen()));
    return buf;
}

class RemoteTest : public ::testing::Test {
protected:
    void SetUp() override {
        env_ = remote_env();
        if (!env_.configured()) {
            GTEST_SKIP() << "HOTKV_REMOTE_HOST/PORT/PASSWORD not set; skipping remote test";
        }
        client_ = hotkv::Client::connect(url_form(env_));
        try {
            client_.ping();
        } catch (const hotkv::ConnectionError& e) {
            if (std::string(e.what()).find("HOTKV_CPP_WITH_TLS") != std::string::npos) {
                GTEST_SKIP() << "this build has no TLS support";
            }
            throw;
        }
        prefix_ = "remote-cpp-" + random_token();
    }

    void TearDown() override {
        for (auto it = cleanups_.rbegin(); it != cleanups_.rend(); ++it) {
            try {
                (*it)();
            } catch (...) {
                // best effort: the unique prefix keeps strays harmless
            }
        }
        if (!keys_.empty()) {
            try {
                client_.del(keys_);
            } catch (...) {
            }
        }
    }

    // A key under this test's prefix, deleted in TearDown.
    std::string key(const std::string& suffix) {
        std::string k = prefix_ + ":" + suffix;
        keys_.push_back(k);
        return k;
    }
    // A name for a namespace/index/bucket (no deletion by DEL; register a
    // cleanup that drops it).
    std::string name(const std::string& suffix) { return prefix_ + "-" + suffix; }
    void cleanup(std::function<void()> fn) { cleanups_.push_back(std::move(fn)); }

    RemoteEnv env_;
    hotkv::Client client_{hotkv::Options{}};
    std::string prefix_;
    std::vector<std::string> keys_;
    std::vector<std::function<void()>> cleanups_;
};

} // namespace

// --- connecting and authenticating ---

TEST_F(RemoteTest, ConnectsWithTheUrlFormAndTheOptionsForm) {
    EXPECT_TRUE(client_.ping());
    EXPECT_EQ(client_.ping("hello from hotkv-cpp"), "hello from hotkv-cpp");
    EXPECT_EQ(client_.echo("round trip"), "round trip");

    hotkv::Client by_options(options_form(env_));
    EXPECT_TRUE(by_options.ping());
    EXPECT_EQ(by_options.client_getname(), std::optional<std::string>("hotkv-cpp-remote-test"));
    EXPECT_GT(by_options.client_id(), 0);

    // Both forms see the same server.
    std::string k = key("shared");
    client_.set(k, "written by the URL client");
    EXPECT_EQ(by_options.get(k), std::optional<std::string>("written by the URL client"));

    std::string info = client_.info("server");
    EXPECT_FALSE(info.empty());
    EXPECT_GT(client_.time().seconds, 1700000000);
}

TEST_F(RemoteTest, WrongOrMissingPasswordFailsCleanly) {
    hotkv::Options bad = options_form(env_);
    bad.password = "this-is-not-the-password";
    try {
        hotkv::Client::connect(bad).ping();
        FAIL() << "expected an authentication failure";
    } catch (const hotkv::ServerError& e) {
        EXPECT_TRUE(e.code() == "WRONGPASS" || e.code() == "ERR") << e.code();
    }

    hotkv::Options none = options_form(env_);
    none.password.clear();
    try {
        hotkv::Client::connect(none).ping();
        FAIL() << "expected NOAUTH";
    } catch (const hotkv::ServerError& e) {
        EXPECT_EQ(e.code(), "NOAUTH");
    }

    // A failed attempt leaves the working client unaffected.
    EXPECT_TRUE(client_.ping());
}

TEST_F(RemoteTest, UntrustedCertificateIsRejectedUnlessVerificationIsSkipped) {
    // No CA: the instance's self-signed certificate is not in the system store.
    hotkv::Options no_ca = options_form(env_);
    no_ca.tls.ca_file.clear();
    EXPECT_THROW(hotkv::Client::connect(no_ca).ping(), hotkv::ConnectionError);

    // The one insecure-skip-verify check.
    hotkv::Options insecure = no_ca;
    insecure.tls.insecure_skip_verify = true;
    EXPECT_TRUE(hotkv::Client::connect(insecure).ping());
}

// --- core data types ---

TEST_F(RemoteTest, StringsExpiryAndCounters) {
    std::string k = key("str");
    EXPECT_FALSE(client_.get(k).has_value());
    EXPECT_TRUE(client_.set(k, "hello"));
    EXPECT_EQ(client_.get(k), std::optional<std::string>("hello"));
    EXPECT_EQ(client_.append(k, " world"), 11);
    EXPECT_EQ(client_.getrange(k, 0, 4), "hello");

    std::string binary("bin\0ary\r\n\xff", 11);
    client_.set(key("bin"), binary);
    EXPECT_EQ(client_.get(prefix_ + ":bin"), std::optional<std::string>(binary));

    std::string c = key("counter");
    EXPECT_EQ(client_.incr(c), 1);
    EXPECT_EQ(client_.incrby(c, 41), 42);
    EXPECT_DOUBLE_EQ(client_.incrbyfloat(key("float"), 1.5), 1.5);
    EXPECT_DOUBLE_EQ(client_.incrbyfloat(prefix_ + ":float", 0.25), 1.75);

    EXPECT_TRUE(client_.expire(k, 100));
    EXPECT_GT(client_.ttl(k), 90);
    EXPECT_LE(client_.ttl(k), 100);
    EXPECT_TRUE(client_.persist(k));
    EXPECT_EQ(client_.ttl(k), -1);
    hotkv::SetOptions px;
    px.px = 300ms;
    client_.set(key("shortlived"), "x", px);
    std::this_thread::sleep_for(500ms);
    EXPECT_FALSE(client_.get(prefix_ + ":shortlived").has_value());
    EXPECT_EQ(client_.del({k, c}), 2);
}

TEST_F(RemoteTest, HashesListsSetsAndSortedSets) {
    std::string h = key("hash");
    client_.hset(h, {{"a", "1"}, {"b", "2"}, {"c", "3"}});
    EXPECT_EQ(client_.hget(h, "b"), std::optional<std::string>("2"));
    EXPECT_EQ(client_.hlen(h), 3);
    EXPECT_EQ(client_.hincrby(h, "a", 10), 11);
    auto all = client_.hgetall(h);
    EXPECT_EQ(all.size(), 3u);
    EXPECT_EQ(client_.hdel(h, {"c"}), 1);

    std::string l = key("list");
    EXPECT_EQ(client_.rpush(l, {"1", "2", "3"}), 3);
    EXPECT_EQ(client_.lpush(l, {"0"}), 4);
    EXPECT_EQ(client_.lrange(l, 0, -1), (std::vector<std::string>{"0", "1", "2", "3"}));
    EXPECT_EQ(client_.lpop(l), std::optional<std::string>("0"));
    EXPECT_EQ(client_.llen(l), 3);

    std::string s = key("set");
    EXPECT_EQ(client_.sadd(s, {"x", "y", "z"}), 3);
    EXPECT_TRUE(client_.sismember(s, "y"));
    EXPECT_EQ(client_.scard(s), 3);
    auto members = client_.smembers(s);
    std::sort(members.begin(), members.end());
    EXPECT_EQ(members, (std::vector<std::string>{"x", "y", "z"}));

    std::string z = key("zset");
    EXPECT_EQ(client_.zadd(z, {{"alice", 100.5}, {"bob", 87}, {"carol", 92.25}}), 3);
    EXPECT_EQ(client_.zrank(z, "bob"), std::optional<int64_t>(0));
    EXPECT_EQ(client_.zscore(z, "carol"), std::optional<double>(92.25));
    auto top = client_.zrange_withscores(z, "0", "-1");
    ASSERT_EQ(top.size(), 3u);
    EXPECT_EQ(top.back().member, "alice");
    EXPECT_DOUBLE_EQ(top.back().score, 100.5);
    EXPECT_DOUBLE_EQ(client_.zincrby(z, 0.1, "bob"), 87.1);
}

TEST_F(RemoteTest, PipelineAndTransaction) {
    std::string a = key("pa");
    std::string b = key("pb");
    auto p = client_.pipeline();
    p.command({"SET", a, "1"}).command({"INCR", a}).command({"SET", b, "x"}).command({"INCR", b}).command({"GET", a});
    auto replies = p.exec();
    ASSERT_EQ(replies.size(), 5u);
    EXPECT_EQ(replies[1].as_int(), 2);
    EXPECT_TRUE(replies[3].is_error()); // INCR on a non-number: error reply, others unaffected
    EXPECT_EQ(replies[4].as_string(), "2");

    std::string t = key("tx");
    auto tx = client_.transaction();
    tx.watch({t});
    tx.multi();
    tx.command({"SET", t, "in-transaction"});
    tx.command({"INCR", key("tx-counter")});
    auto results = tx.exec();
    ASSERT_TRUE(results.has_value());
    EXPECT_EQ(results->size(), 2u);
    EXPECT_EQ(client_.get(t), std::optional<std::string>("in-transaction"));
}

TEST_F(RemoteTest, OneMegabyteValueRoundTripsOverTls) {
    std::string big(1 << 20, '\0');
    std::mt19937 gen(12345);
    for (char& ch : big) {
        ch = static_cast<char>(gen());
    }
    std::string k = key("big");
    auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(client_.set(k, big));
    auto got = client_.get(k);
    auto elapsed = std::chrono::steady_clock::now() - start;
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), big.size());
    EXPECT_TRUE(*got == big);
    EXPECT_EQ(client_.strlen(k), static_cast<int64_t>(big.size()));
    std::printf("[remote] 1 MiB set+get over the internet in %lld ms\n",
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
}

TEST_F(RemoteTest, ConcurrentUseOfOneClient) {
    constexpr int kThreads = 8;
    constexpr int kOps = 60;
    std::string counter = key("concurrent-counter");
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            try {
                for (int i = 0; i < kOps; ++i) {
                    std::string k = prefix_ + ":conc:" + std::to_string(t) + ":" + std::to_string(i);
                    client_.set(k, std::to_string(t * 1000 + i));
                    auto v = client_.get(k);
                    if (!v || *v != std::to_string(t * 1000 + i)) {
                        ++errors;
                    }
                    client_.incr(counter);
                    client_.del({k});
                }
            } catch (...) {
                ++errors;
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(client_.get(counter), std::optional<std::string>(std::to_string(kThreads * kOps)));
}

TEST_F(RemoteTest, PublishReachesASubscriberOverTls) {
    std::string channel = name("chan");
    auto sub = client_.create_subscriber();
    sub.subscribe({channel});
    EXPECT_EQ(client_.publish(channel, "ping over tls"), 1);
    auto msg = sub.next_message(10s);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->channel, channel);
    EXPECT_EQ(msg->payload, "ping over tls");
}

// --- untyped commands through the generic call ---

TEST_F(RemoteTest, UntypedCommandsThroughTheGenericCall) {
    std::string k = key("generic");
    client_.set(k, "v");
    EXPECT_EQ(client_.command({"OBJECT", "ENCODING", k}).as_string().empty(), false);
    EXPECT_EQ(client_.execute("STRLEN", k).as_int(), 1);
    EXPECT_EQ(client_.execute("EXPIRE", k, 100).as_int(), 1);
    EXPECT_DOUBLE_EQ(client_.execute("INCRBYFLOAT", key("generic-f"), 0.5).as_double(), 0.5);

    // Probabilistic structures have no typed wrapper.
    std::string bf = key("bloom");
    EXPECT_EQ(client_.command({"BF.RESERVE", bf, "0.01", "1000"}).as_string(), "OK");
    EXPECT_EQ(client_.command({"BF.ADD", bf, "item"}).as_int(), 1);
    EXPECT_EQ(client_.command({"BF.EXISTS", bf, "item"}).as_int(), 1);
    EXPECT_EQ(client_.command({"BF.EXISTS", bf, "other"}).as_int(), 0);
}

// --- licensed HotKV and AI commands (typed) ---

TEST_F(RemoteTest, TagsGcraAndHotKeysReadOnly) {
    std::string k = key("tagged");
    client_.set(k, "v");
    EXPECT_EQ(client_.tag().set(k, {"alpha", "beta"}), 2);
    auto tags = client_.tag().get(k);
    std::sort(tags.begin(), tags.end());
    EXPECT_EQ(tags, (std::vector<std::string>{"alpha", "beta"}));
    EXPECT_TRUE(client_.tag().exists(k, "alpha"));
    EXPECT_EQ(client_.tag().rem(k, {"alpha", "beta"}), 2);

    std::string g = key("gcra");
    EXPECT_FALSE(client_.gcra().limit(g, 10000, 1).denied);
    EXPECT_TRUE(client_.gcra().limit(g, 10000, 1).denied);
    cleanup([&, g] { client_.gcra().reset({g}); });

    EXPECT_NO_THROW(client_.hotkeys().top(3)); // read only; HOTKEYS RESET is global and not used
}

TEST_F(RemoteTest, VectorIndexAndSnapshots) {
    std::string index = name("vsim");
    client_.vsim().create_index(index, 3);
    cleanup([&, index] { client_.vsim().drop_index(index); });
    EXPECT_TRUE(client_.vsim().set(index, "v1", {1.0, 0.0, 0.0}));
    EXPECT_TRUE(client_.vsim().set(index, "v2", {0.0, 1.0, 0.0}));
    auto hits = client_.vsim().search(index, 2, {0.9, 0.1, 0.0});
    ASSERT_FALSE(hits.empty());
    EXPECT_EQ(hits[0].key, "v1");
    EXPECT_EQ(client_.vsim().info(index).vectors, 2);

    // Snapshot names are file names on the server.
    std::string snap = "remotecpp" + random_token();
    EXPECT_GT(client_.vsim().save(snap, "snapshot-blob"), 0);
    cleanup([&, snap] { client_.vsim().drop_snapshot(snap); });
    EXPECT_EQ(client_.vsim().load(snap), std::optional<std::string>("snapshot-blob"));
    auto snaps = client_.vsim().list_snapshots();
    EXPECT_NE(std::find(snaps.begin(), snaps.end(), snap), snaps.end());
}

TEST_F(RemoteTest, SemanticAndPromptCaches) {
    std::string sc = name("scache");
    client_.scache().create(sc, 3);
    cleanup([&, sc] { client_.scache().drop(sc); });
    hotkv::Usage usage;
    usage.model = "gpt-4";
    usage.input_tokens = 10;
    usage.output_tokens = 4;
    client_.scache().set(sc, "capital of france", "Paris", {1.0, 0.0, 0.0}, std::nullopt, usage);
    auto hit = client_.scache().get(sc, {1.0, 0.0, 0.0});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->response, "Paris");
    EXPECT_FALSE(client_.scache().get(sc, {0.0, 0.0, 1.0}, 0.99).has_value());
    EXPECT_GE(client_.scache().stats(sc).entries, 1);
    client_.scache().set_threshold(sc, 0.75);

    std::string pc = name("pcache");
    client_.pcache().create(pc);
    cleanup([&, pc] { client_.pcache().drop(pc); });
    client_.pcache().set(pc, "prompt", "response", std::nullopt, {}, usage);
    EXPECT_EQ(client_.pcache().get(pc, "prompt"), std::optional<std::string>("response"));
    client_.pcache().config_set_cost_per_hit(pc, 0.0025);
    client_.pcache().config_set_ttl(pc, 3600);
    EXPECT_GE(client_.pcache().stats(pc).entries, 1);

    // The get-or-call helpers.
    hotkv::ai::PromptCache helper(client_, name("pcache-helper"), 600);
    helper.create_namespace();
    cleanup([&] { client_.pcache().drop(name("pcache-helper")); });
    int calls = 0;
    hotkv::ai::PromptCacheKeyInput input;
    input.provider = "openai";
    input.model = "gpt-4o";
    input.messages = {{"user", "2+2?"}};
    auto fn = [&]() -> hotkv::ai::LlmCallResult {
        ++calls;
        hotkv::ai::LlmUsage u;
        u.model = "gpt-4";
        u.input_tokens = 5;
        u.output_tokens = 1;
        return {"4", u};
    };
    EXPECT_FALSE(helper.get_or_call(input, fn).hit);
    EXPECT_TRUE(helper.get_or_call(input, fn).hit);
    EXPECT_EQ(calls, 1);
}

TEST_F(RemoteTest, KeyedCaches) {
    struct Group {
        const char* label;
        hotkv::KeyedCacheCommands cmds;
    };
    std::vector<Group> groups{{"tool", client_.tool_cache()},
                              {"ncache", client_.ncache()},
                              {"guard", client_.guard()},
                              {"rerank", client_.rerank()}};
    for (auto& g : groups) {
        std::string ns = name(std::string("keyed-") + g.label);
        g.cmds.create(ns);
        hotkv::KeyedCacheCommands cmds = g.cmds;
        cleanup([cmds, ns]() mutable { cmds.drop(ns); });
        hotkv::Usage usage;
        usage.model = "gpt-4";
        usage.input_tokens = 3;
        g.cmds.set(ns, "k1", "v1", std::nullopt, {}, usage);
        EXPECT_EQ(g.cmds.get(ns, "k1"), std::optional<std::string>("v1")) << g.label;
        EXPECT_GE(g.cmds.stats(ns).entries, 1) << g.label;
        EXPECT_TRUE(g.cmds.del(ns, "k1")) << g.label;
        EXPECT_FALSE(g.cmds.get(ns, "k1").has_value()) << g.label;
    }
}

TEST_F(RemoteTest, LlmBudgetStatsAndPrices) {
    std::string budget = name("budget");
    client_.llmbudget().set(budget, 1000000, 12.5, 3600);
    cleanup([&, budget] { client_.llmbudget().drop(budget); });
    EXPECT_NEAR(client_.llmbudget().status(budget).cost_limit_usd, 12.5, 1e-9);
    client_.llmbudget().set(budget, 1000000, 0.000001, 3600); // one micro-dollar
    EXPECT_NEAR(client_.llmbudget().status(budget).cost_limit_usd, 0.000001, 1e-12);
    client_.llmbudget().set(budget, 1000000, 1e-5, 3600);
    EXPECT_NEAR(client_.llmbudget().status(budget).cost_limit_usd, 0.00001, 1e-12);
    client_.llmbudget().set(budget, 1000000, 100.0, 3600);
    EXPECT_TRUE(client_.llmbudget().consume(budget, 100, 0.25));
    EXPECT_FALSE(client_.llmbudget().consume(budget, 100, 500.0)); // over the cap
    EXPECT_NEAR(client_.llmbudget().status(budget).cost_used_usd, 0.25, 1e-6);

    std::string model = name("model");
    client_.llmstats().price_set(model, 3.0, 15.0);
    cleanup([&, model] { client_.llmstats().price_del(model); });
    auto price = client_.llmstats().price_get(model);
    EXPECT_TRUE(price.priced);
    EXPECT_NEAR(price.output_usd_per_mtok, 15.0, 1e-9);

    std::string bucket = name("stats");
    client_.llmstats().bucket(bucket);
    cleanup([&, bucket] { client_.llmstats().drop(bucket); });
    client_.llmstats().record(bucket, 100, 50, 0, 0.01, 200);
    hotkv::LlmStatsRecordOptions priced;
    priced.model = model;
    client_.llmstats().record(bucket, 1000000, 0, 0, 0.0, 10, priced); // $3
    auto snap = client_.llmstats().get(bucket);
    EXPECT_EQ(snap.requests, 2);
    EXPECT_NEAR(snap.cost_usd, 3.01, 1e-6);
}

TEST_F(RemoteTest, PromptRegistryAndEmbedDedup) {
    std::string prompt = name("prompt");
    EXPECT_EQ(client_.prompt().put(prompt, "You are helpful."), 1);
    cleanup([&, prompt] { client_.prompt().drop(prompt); });
    EXPECT_EQ(client_.prompt().put(prompt, "You are very helpful."), 2);
    EXPECT_EQ(client_.prompt().latest(prompt)->content, "You are very helpful.");
    EXPECT_EQ(client_.prompt().get(prompt, 1)->content, "You are helpful.");
    client_.prompt().set_active(prompt, 1);
    EXPECT_EQ(client_.prompt().versions(prompt), (std::vector<int64_t>{1, 2}));

    std::string ns = name("dedup");
    EXPECT_TRUE(client_.embed_dedup().put(ns, "hash1", "text-embedding-3-small", "0.1,0.2,0.3"));
    cleanup([&, ns] { client_.embed_dedup().drop(ns); });
    EXPECT_FALSE(client_.embed_dedup().put(ns, "hash1", "text-embedding-3-small", "0.1,0.2,0.3"));
    EXPECT_EQ(client_.embed_dedup().get(ns, "hash1"), std::optional<std::string>("0.1,0.2,0.3"));
    EXPECT_GE(client_.embed_dedup().stats(ns).entries, 1);
}

TEST_F(RemoteTest, AgentMemory) {
    std::string session = name("amem-session");
    client_.amem().conv().create(session, 10);
    cleanup([&, session] { client_.amem().conv().drop(session); });
    client_.amem().conv().append(session, "user", "hi");
    client_.amem().conv().append(session, "assistant", "hello");
    EXPECT_EQ(client_.amem().conv().get(session).size(), 2u);
    EXPECT_EQ(client_.amem().conv().window(session, 1)[0].role, "assistant");

    std::string agent = name("amem-agent");
    client_.amem().sem().create(agent, 3);
    cleanup([&, agent] { client_.amem().sem().forget(agent, "fact1"); });
    client_.amem().sem().store(agent, "fact1", "the sky is blue", {1.0, 0.0, 0.0});
    auto recalled = client_.amem().sem().recall(agent, 1, {1.0, 0.0, 0.0});
    ASSERT_FALSE(recalled.empty());
    EXPECT_EQ(recalled[0].key, "fact1");
    EXPECT_FALSE(client_.amem().search(agent, 3, {1.0, 0.0, 0.0}).empty());

    std::string work = name("amem-work");
    client_.amem().work().set(work, "scratch", "value");
    cleanup([&, work] { client_.amem().work().clear(work); });
    EXPECT_EQ(client_.amem().work().get(work, "scratch"), std::optional<std::string>("value"));

    std::string epis = name("amem-epis");
    client_.amem().epis().log(epis, "tool_call", "search()");
    cleanup([&, epis] { client_.amem().epis().clear(epis); });
    EXPECT_EQ(client_.amem().epis().stats(epis).total_events, 1);

    std::string summ = name("amem-summary");
    EXPECT_GE(client_.amem().summary().set(summ, "A summary."), 1);
    cleanup([&, summ] { client_.amem().summary().drop(summ); });
    EXPECT_TRUE(client_.amem().summary().get(summ).has_value());
}

TEST_F(RemoteTest, RagRateLimitTimeSeriesAndFeatureStore) {
    std::string rag = name("rag");
    client_.rag().create(rag, 3);
    cleanup([&, rag] { client_.rag().drop(rag); });
    EXPECT_GE(client_.rag().ingest(rag, "doc1", "The quick brown fox jumps over the lazy dog."), 1);
    hotkv::RagSearchOptions rs;
    rs.k = 3;
    EXPECT_FALSE(client_.rag().search(rag, "quick fox", rs).empty());
    EXPECT_GE(client_.rag().stats(rag).total_chunks, 1);

    std::string rl = name("ratelimit");
    client_.ratelimit().create(rl, 2, 10000);
    cleanup([&, rl] { client_.ratelimit().drop(rl); });
    EXPECT_TRUE(client_.ratelimit().check(rl, "u1").allowed);
    client_.ratelimit().check(rl, "u1");
    EXPECT_FALSE(client_.ratelimit().check(rl, "u1").allowed);
    std::string tb = name("tokenbucket");
    hotkv::RateLimitCreateOptions tbo;
    tbo.algo = "token_bucket";
    tbo.refill = 0.5;
    client_.ratelimit().create(tb, 10, 1000, tbo);
    cleanup([&, tb] { client_.ratelimit().drop(tb); });
    EXPECT_TRUE(client_.ratelimit().check(tb, "u1").allowed);

    std::string ts = key("ts");
    hotkv::TsCreateOptions tso;
    tso.labels = {{"remote_cpp", prefix_}};
    client_.ts().create(ts, tso);
    cleanup([&, ts] { client_.ts().del(ts); });
    client_.ts().add(ts, 1000, 1.5);
    client_.ts().add(ts, 2000, -2.25);
    client_.ts().add(ts, 3000, 0.1);
    auto range = client_.ts().range(ts, "0", "+");
    ASSERT_EQ(range.size(), 3u);
    EXPECT_DOUBLE_EQ(range[1].value, -2.25);
    EXPECT_EQ(client_.ts().info(ts).total_samples, 3);

    std::string group = name("features");
    client_.feature().create(group, "remote test", {{"age", "int64"}, {"score", "double"}});
    cleanup([&, group] { client_.feature().drop(group); });
    client_.feature().set(group, "user1", {{"age", "30"}, {"score", "0.75"}});
    auto rec = client_.feature().get(group, "user1");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->values.size(), 2u);
    EXPECT_EQ(client_.feature().schema(group).size(), 2u);
}
