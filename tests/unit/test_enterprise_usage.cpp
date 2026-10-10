// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

// Unit tests (no HotKV server): exact wire
// argument lists for the shared token usage group and every new option,
// captured with a loopback fake server (this process plays both client
// and server over a real socket, since hotkv::Client has no lower-level
// seam to inject a fake stream into; no HotKV server is involved), plus
// by-field-name parsing of hand-built RESP2 flat arrays and RESP3 maps
// for LLMSTATS.PRICE and the other changed
// STATS/GET/STATUS replies.

// The loopback fake server below uses POSIX sockets; on Windows this
// file compiles to nothing (the same wire checks run on POSIX CI).
#ifndef _WIN32

#include <hotkv/ai.hpp>
#include <hotkv/hotkv.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace hotkv;

namespace {

// Accepts one loopback connection, decodes the one RESP array-of-bulk-
// strings command sent (the wire form every command uses; see
// detail::encode_command), hands it back as argv(), and writes a canned
// raw reply. Runs its accept/decode/reply loop on a background thread so
// the calling test can issue its one command synchronously exactly like
// real code would.
class LoopbackFakeServer {
public:
    LoopbackFakeServer() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listen_fd_, 1);
        socklen_t alen = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &alen);
        port_ = ntohs(addr.sin_port);
    }

    ~LoopbackFakeServer() {
        if (thread_.joinable()) {
            thread_.join();
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
        }
    }

    LoopbackFakeServer(const LoopbackFakeServer&) = delete;
    LoopbackFakeServer& operator=(const LoopbackFakeServer&) = delete;

    int port() const { return port_; }

    // Starts serving in the background; call join() after the
    // client-side call under test has returned.
    void serve_once(std::string raw_reply) {
        thread_ = std::thread([this, reply = std::move(raw_reply)]() mutable {
            int conn = ::accept(listen_fd_, nullptr, nullptr);
            if (conn < 0) {
                return;
            }
            std::string buf;
            argv_ = read_command(conn, buf);
            ::send(conn, reply.data(), reply.size(), 0);
            ::close(conn);
        });
    }

    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    const std::vector<std::string>& argv() const { return argv_; }

private:
    static bool fill(int fd, std::string& buf, std::size_t need) {
        char chunk[4096];
        while (buf.size() < need) {
            ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                return false;
            }
            buf.append(chunk, static_cast<std::size_t>(n));
        }
        return true;
    }

    static std::string read_line(int fd, std::string& buf, std::size_t& pos) {
        for (;;) {
            std::size_t nl = buf.find("\r\n", pos);
            if (nl != std::string::npos) {
                std::string line = buf.substr(pos, nl - pos);
                pos = nl + 2;
                return line;
            }
            if (!fill(fd, buf, buf.size() + 1)) {
                return {};
            }
        }
    }

    static std::vector<std::string> read_command(int fd, std::string& buf) {
        std::vector<std::string> out;
        std::size_t pos = 0;
        std::string header = read_line(fd, buf, pos);
        if (header.empty() || header[0] != '*') {
            return out;
        }
        int argc = std::stoi(header.substr(1));
        for (int i = 0; i < argc; ++i) {
            std::string len_line = read_line(fd, buf, pos);
            std::size_t len = static_cast<std::size_t>(std::stoi(len_line.substr(1)));
            if (!fill(fd, buf, pos + len + 2)) {
                return out;
            }
            out.push_back(buf.substr(pos, len));
            pos += len + 2;
        }
        return out;
    }

    int listen_fd_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::vector<std::string> argv_;
};

// A Client that will talk to fake on its first command. use_resp3 is
// off so the fake server's single reply does not need to also answer a
// HELLO handshake.
Client make_client(const LoopbackFakeServer& fake) {
    Options opts;
    opts.host = "127.0.0.1";
    opts.port = static_cast<uint16_t>(fake.port());
    opts.use_resp3 = false;
    opts.connect_timeout = std::chrono::milliseconds(2000);
    opts.command_timeout = std::chrono::milliseconds(2000);
    return Client::connect(opts);
}

std::string resp_bulk(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}
std::string resp_int(int64_t n) {
    return ":" + std::to_string(n) + "\r\n";
}

using Fields = std::vector<std::pair<std::string, std::string>>;

// Renders a flat RESP2 array of alternating name/value bulk strings,
// integer-typed per int_fields.
std::string flat_array(const Fields& fields, const std::vector<std::string>& int_fields = {}) {
    std::string out = "*" + std::to_string(fields.size() * 2) + "\r\n";
    for (const auto& [k, v] : fields) {
        out += resp_bulk(k);
        bool is_int = std::find(int_fields.begin(), int_fields.end(), k) != int_fields.end();
        out += is_int ? resp_int(std::stoll(v)) : resp_bulk(v);
    }
    return out;
}

// Renders a RESP3 map of name/value bulk strings, integer-typed per int_fields.
std::string flat_map(const Fields& fields, const std::vector<std::string>& int_fields = {}) {
    std::string out = "%" + std::to_string(fields.size()) + "\r\n";
    for (const auto& [k, v] : fields) {
        out += resp_bulk(k);
        bool is_int = std::find(int_fields.begin(), int_fields.end(), k) != int_fields.end();
        out += is_int ? resp_int(std::stoll(v)) : resp_bulk(v);
    }
    return out;
}

} // namespace

// --- exact wire argument lists: PCACHE.SET / SCACHE.SET / keyed cache SET ---

TEST(EnterpriseUsageWire, ScacheSetNoUsage) {
    LoopbackFakeServer fake;
    fake.serve_once("+OK\r\n");
    Client client = make_client(fake);
    client.scache().set("n", "q", "r", {0.5, 0.25});
    fake.join();
    EXPECT_EQ(fake.argv(), (std::vector<std::string>{"SCACHE.SET", "n", "q", "r", "0.5", "0.25"}));
}

TEST(EnterpriseUsageWire, ScacheSetWithUsage) {
    LoopbackFakeServer fake;
    fake.serve_once("+OK\r\n");
    Client client = make_client(fake);
    Usage usage;
    usage.model = "gpt-4";
    usage.cache_write_5m_tokens = 7;
    client.scache().set("n", "q", "r", {0.5, 0.25}, std::nullopt, usage);
    fake.join();
    EXPECT_EQ(fake.argv(), (std::vector<std::string>{"SCACHE.SET", "n", "q", "r", "0.5", "0.25", "MODEL", "gpt-4",
                                                       "CACHEWRITE5M", "7"}));
}

TEST(EnterpriseUsageWire, PcacheSetTtlMetaAndUsage) {
    LoopbackFakeServer fake;
    fake.serve_once("+OK\r\n");
    Client client = make_client(fake);
    Usage usage;
    usage.model = "gpt-4";
    usage.input_tokens = 12;
    usage.output_tokens = 8;
    usage.cache_read_tokens = 4;
    client.pcache().set("n", "p", "r", 30, "{\"k\":\"v\"}", usage);
    fake.join();
    EXPECT_EQ(fake.argv(), (std::vector<std::string>{"PCACHE.SET", "n", "p", "r", "TTL", "30", "META",
                                                       "{\"k\":\"v\"}", "MODEL", "gpt-4", "INPUT", "12", "OUTPUT",
                                                       "8", "CACHEREAD", "4"}));
}

TEST(EnterpriseUsageWire, KeyedCacheSetWithUsage) {
    LoopbackFakeServer fake;
    fake.serve_once("+OK\r\n");
    Client client = make_client(fake);
    Usage usage;
    usage.model = "gpt-4";
    usage.cache_write_1h_tokens = 9;
    client.tool_cache().set("n", "k", "v", 60, "meta", usage);
    fake.join();
    EXPECT_EQ(fake.argv(), (std::vector<std::string>{"TOOL.CACHE.SET", "n", "k", "v", "TTL", "60", "META", "meta",
                                                       "MODEL", "gpt-4", "CACHEWRITE1H", "9"}));
}

// --- exact wire argument lists: LLMSTATS.RECORD / LLMSTATS.PRICE / LLMBUDGET.CONSUME ---

TEST(EnterpriseUsageWire, LlmStatsRecordNoOptions) {
    LoopbackFakeServer fake;
    fake.serve_once("+OK\r\n");
    Client client = make_client(fake);
    client.llmstats().record("bucket", 100, 50, 0, 0.002, 120);
    fake.join();
    EXPECT_EQ(fake.argv(),
              (std::vector<std::string>{"LLMSTATS.RECORD", "bucket", "100", "50", "0", "0.002", "120"}));
}

TEST(EnterpriseUsageWire, LlmStatsRecordModelAndCacheWrites) {
    LoopbackFakeServer fake;
    fake.serve_once("+OK\r\n");
    Client client = make_client(fake);
    LlmStatsRecordOptions opts;
    opts.error = false;
    opts.model = "claude-sonnet-5";
    opts.cache_write_5m_tokens = 20;
    opts.cache_write_1h_tokens = 30;
    client.llmstats().record("bucket", 100, 50, 10, 0, 80, opts);
    fake.join();
    EXPECT_EQ(fake.argv(), (std::vector<std::string>{"LLMSTATS.RECORD", "bucket", "100", "50", "10", "0", "80",
                                                       "ERROR", "0", "MODEL", "claude-sonnet-5", "CACHEWRITE5M",
                                                       "20", "CACHEWRITE1H", "30"}));
}

TEST(EnterpriseUsageWire, LlmStatsPriceSetRequiredOnly) {
    LoopbackFakeServer fake;
    fake.serve_once("+OK\r\n");
    Client client = make_client(fake);
    client.llmstats().price_set("my-model", 3, 15);
    fake.join();
    EXPECT_EQ(fake.argv(),
              (std::vector<std::string>{"LLMSTATS.PRICE", "SET", "my-model", "INPUT", "3", "OUTPUT", "15"}));
}

TEST(EnterpriseUsageWire, LlmStatsPriceSetAllOverrides) {
    LoopbackFakeServer fake;
    fake.serve_once("+OK\r\n");
    Client client = make_client(fake);
    LlmPriceOptions opts;
    opts.cache_read_usd_per_mtok = 0.5;
    opts.cache_write_5m_usd_per_mtok = 4;
    opts.cache_write_1h_usd_per_mtok = 6;
    client.llmstats().price_set("my-model", 3, 15, opts);
    fake.join();
    EXPECT_EQ(fake.argv(), (std::vector<std::string>{"LLMSTATS.PRICE", "SET", "my-model", "INPUT", "3", "OUTPUT",
                                                       "15", "CACHEREAD", "0.5", "CACHEWRITE5M", "4",
                                                       "CACHEWRITE1H", "6"}));
}

TEST(EnterpriseUsageWire, LlmBudgetConsumeLegacy) {
    LoopbackFakeServer fake;
    fake.serve_once(":1\r\n");
    Client client = make_client(fake);
    EXPECT_TRUE(client.llmbudget().consume("name", 10, 0.01));
    fake.join();
    EXPECT_EQ(fake.argv(), (std::vector<std::string>{"LLMBUDGET.CONSUME", "name", "10", "0.01"}));
}

TEST(EnterpriseUsageWire, LlmBudgetConsumeWithUsage) {
    LoopbackFakeServer fake;
    fake.serve_once(":1\r\n");
    Client client = make_client(fake);
    Usage usage;
    usage.model = "gpt-4";
    usage.input_tokens = 10;
    usage.output_tokens = 5;
    EXPECT_TRUE(client.llmbudget().consume("name", 0, 0.0, usage));
    fake.join();
    EXPECT_EQ(fake.argv(), (std::vector<std::string>{"LLMBUDGET.CONSUME", "name", "0", "0", "MODEL", "gpt-4",
                                                       "INPUT", "10", "OUTPUT", "5"}));
}

// --- canned reply parsing: RESP2 flat arrays and RESP3 maps, by field name ---

Fields price_fields(const std::string& model, const std::string& source, bool priced, bool retired) {
    return {
        {"model", model},
        {"source", source},
        {"priced", priced ? "1" : "0"},
        {"retired", retired ? "1" : "0"},
        {"input_usd_per_mtok", "3.000000"},
        {"output_usd_per_mtok", "15.000000"},
        {"cache_read_usd_per_mtok", "0.300000"},
        {"cache_write_5m_usd_per_mtok", "3.750000"},
        {"cache_write_1h_usd_per_mtok", "6.000000"},
    };
}

TEST(EnterpriseUsageParsing, LlmPriceGetParsesResp3Map) {
    LoopbackFakeServer fake;
    fake.serve_once(flat_map(price_fields("claude-sonnet-5", "custom", true, false), {"priced", "retired"}));
    Client client = make_client(fake);
    LlmPrice price = client.llmstats().price_get("claude-sonnet-5");
    fake.join();
    EXPECT_EQ(price.model, "claude-sonnet-5");
    EXPECT_EQ(price.source, "custom");
    EXPECT_TRUE(price.priced);
    EXPECT_FALSE(price.retired);
    EXPECT_DOUBLE_EQ(price.input_usd_per_mtok, 3.0);
    EXPECT_DOUBLE_EQ(price.cache_write_5m_usd_per_mtok, 3.75);
}

TEST(EnterpriseUsageParsing, LlmPriceUnknownModelIsNotAnError) {
    LoopbackFakeServer fake;
    fake.serve_once(flat_map(price_fields("unknown-model", "unpriced", false, false), {"priced", "retired"}));
    Client client = make_client(fake);
    LlmPrice price = client.llmstats().price_get("unknown-model");
    fake.join();
    EXPECT_EQ(price.source, "unpriced");
    EXPECT_FALSE(price.priced);
}

TEST(EnterpriseUsageParsing, LlmPriceListParsesMultipleEntries) {
    std::string raw = "*2\r\n" + flat_map(price_fields("model-a", "default", true, false), {"priced", "retired"}) +
                       flat_map(price_fields("model-b", "custom", true, true), {"priced", "retired"});
    LoopbackFakeServer fake;
    fake.serve_once(raw);
    Client client = make_client(fake);
    std::vector<LlmPrice> all = client.llmstats().price_list();
    fake.join();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].model, "model-a");
    EXPECT_EQ(all[1].model, "model-b");
    EXPECT_TRUE(all[1].retired);
}

TEST(EnterpriseUsageParsing, ScacheStatsParsesResp3MapWithUsageSaved) {
    Fields f{
        {"entries", "3"},   {"hits", "10"},
        {"misses", "2"},    {"hit_rate", "0.833333"},
        {"p50_us", "150"},  {"usage_saved_requests", "10"},
        {"usage_saved_input_tokens", "120"},
        {"usage_saved_output_tokens", "80"},
        {"usage_saved_cache_read_tokens", "0"},
        {"usage_saved_cache_write_tokens", "0"},
        {"usage_saved_cost_usd", "0.000160"},
    };
    LoopbackFakeServer fake;
    fake.serve_once(flat_map(f, {"entries", "hits", "misses", "p50_us", "usage_saved_requests",
                                  "usage_saved_input_tokens", "usage_saved_output_tokens",
                                  "usage_saved_cache_read_tokens", "usage_saved_cache_write_tokens"}));
    Client client = make_client(fake);
    SCacheStats stats = client.scache().stats("ns");
    fake.join();
    EXPECT_EQ(stats.entries, 3);
    EXPECT_EQ(stats.hits, 10);
    EXPECT_DOUBLE_EQ(stats.hit_rate, 0.833333);
    EXPECT_EQ(stats.usage.requests, 10);
    EXPECT_EQ(stats.usage.input_tokens, 120);
    EXPECT_DOUBLE_EQ(stats.usage.cost_usd, 0.00016);
}

TEST(EnterpriseUsageParsing, PcacheStatsParsesResp2FlatArrayMissingFieldsDefaultZero) {
    Fields f{{"entries", "1"}, {"hits", "4"}};
    LoopbackFakeServer fake;
    fake.serve_once(flat_array(f, {"entries", "hits"}));
    Client client = make_client(fake);
    PCacheStats stats = client.pcache().stats("ns");
    fake.join();
    EXPECT_EQ(stats.entries, 1);
    EXPECT_EQ(stats.hits, 4);
    EXPECT_EQ(stats.misses, 0);
    EXPECT_DOUBLE_EQ(stats.cost_saved_usd, 0.0);
    EXPECT_EQ(stats.usage.requests, 0);
}

TEST(EnterpriseUsageParsing, LlmStatsGetParsesResp2FlatArray) {
    Fields f{
        {"requests", "10"},        {"errors", "1"},
        {"input_tokens", "500"},   {"output_tokens", "200"},
        {"cached_tokens", "50"},   {"cost_usd", "0.0123"},
        {"avg_latency_ms", "42.5"}, {"requests_1m", "3"},
        {"tokens_1m", "150"},      {"cost_usd_1m", "0.004"},
        {"cache_write_5m_tokens", "20"},
        {"cache_write_1h_tokens", "0"},
        {"unpriced_requests", "2"},
    };
    LoopbackFakeServer fake;
    fake.serve_once(flat_array(f, {"requests", "errors", "input_tokens", "output_tokens", "cached_tokens",
                                    "requests_1m", "tokens_1m", "cache_write_5m_tokens", "cache_write_1h_tokens",
                                    "unpriced_requests"}));
    Client client = make_client(fake);
    LlmStatsSnapshot snap = client.llmstats().get("bucket");
    fake.join();
    EXPECT_EQ(snap.requests, 10);
    EXPECT_DOUBLE_EQ(snap.cost_usd, 0.0123);
    EXPECT_EQ(snap.cache_write_5m_tokens, 20);
    EXPECT_EQ(snap.unpriced_requests, 2);
}

TEST(EnterpriseUsageParsing, LlmBudgetStatusParsesResp3Map) {
    Fields f{
        {"tokens_used", "100"},        {"token_limit", "1000"},     {"cost_used_usd", "0.01"},
        {"cost_limit_usd", "5.00"},    {"window_seconds", "3600"},  {"window_remaining", "1200"},
        {"requests_allowed", "9"},     {"requests_rejected", "1"},  {"unpriced_requests", "0"},
    };
    LoopbackFakeServer fake;
    fake.serve_once(flat_map(f, {"tokens_used", "token_limit", "window_seconds", "window_remaining",
                                  "requests_allowed", "requests_rejected", "unpriced_requests"}));
    Client client = make_client(fake);
    LlmBudgetStatus status = client.llmbudget().status("name");
    fake.join();
    EXPECT_EQ(status.tokens_used, 100);
    EXPECT_EQ(status.requests_allowed, 9);
    EXPECT_EQ(status.unpriced_requests, 0);
}

#endif // !_WIN32
