// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

// ClusterClient redirect handling against scripted in-process servers
// (see fake_server.hpp). These run the real client stack (sockets, pool,
// pipeline) and check the wire: which node saw which command, in what
// order, on which connection, and what the slot map did afterwards. No
// real cluster is involved.

#ifndef _WIN32

#include "fake_server.hpp"

#include <hotkv/hotkv.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>

using hotkv::ClusterClient;
using hotkv::ConnectionError;
using hotkv::Options;
using hotkv::ServerError;
using hotkv::testutil::FakeServer;
using hotkv::testutil::RecordedCommand;

namespace {

// CLUSTER SLOTS reply: ranges of (start, end, port), each owned by 127.0.0.1:port.
struct SlotRange {
    int start;
    int end;
    uint16_t port;
};

std::string slots_reply(const std::vector<SlotRange>& ranges) {
    std::string out = "*" + std::to_string(ranges.size()) + "\r\n";
    for (const auto& r : ranges) {
        out += "*3\r\n:" + std::to_string(r.start) + "\r\n:" + std::to_string(r.end) + "\r\n";
        out += "*2\r\n$9\r\n127.0.0.1\r\n:" + std::to_string(r.port) + "\r\n";
    }
    return out;
}

std::string bulk(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string redirect(const char* kind, int slot, uint16_t port) {
    return std::string("-") + kind + " " + std::to_string(slot) + " 127.0.0.1:" + std::to_string(port) + "\r\n";
}

Options options_for(uint16_t port) {
    Options o;
    o.host = "127.0.0.1";
    o.port = port;
    o.pool_size = 2;
    o.command_timeout = std::chrono::milliseconds(2000);
    return o;
}

std::size_t count_named(const std::vector<RecordedCommand>& cmds, const std::string& name) {
    std::size_t n = 0;
    for (const auto& c : cmds) {
        if (!c.argv.empty() && c.argv[0] == name) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST(ClusterRedirects, MovedRefreshesSlotMapAndRetriesOnTheNamedNode) {
    std::atomic<uint16_t> port_b{0};
    FakeServer b([](int, const std::vector<std::string>& argv) {
        if (argv[0] == "GET") {
            return bulk("from-b");
        }
        return FakeServer::handshake_reply(argv);
    });
    port_b = b.port();
    std::atomic<uint16_t> port_a{0};
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_a}});
        }
        if (argv[0] == "GET") {
            return redirect("MOVED", hotkv::key_slot("k"), port_b);
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();

    ClusterClient cluster(options_for(a.port()));
    hotkv::Reply r = cluster.command({"GET", "k"});
    EXPECT_EQ(r.as_string(), "from-b");

    // A saw: the initial CLUSTER SLOTS, the GET that bounced, and a second
    // CLUSTER SLOTS (the refresh after MOVED).
    auto a_cmds = a.commands();
    EXPECT_EQ(count_named(a_cmds, "CLUSTER"), 2u);
    EXPECT_EQ(count_named(a_cmds, "GET"), 1u);
    EXPECT_EQ(count_named(b.commands(), "GET"), 1u);

    // The slot is now pinned to B: the next GET never touches A.
    EXPECT_EQ(cluster.command({"GET", "k"}).as_string(), "from-b");
    EXPECT_EQ(count_named(a.commands(), "GET"), 1u);
    EXPECT_EQ(count_named(b.commands(), "GET"), 2u);
}

TEST(ClusterRedirects, AskSendsAskingThenTheCommandOnOneConnectionAndLeavesTheMapAlone) {
    FakeServer b([](int, const std::vector<std::string>& argv) {
        if (argv[0] == "ASKING") {
            return std::string("+OK\r\n");
        }
        if (argv[0] == "GET") {
            return bulk("migrated");
        }
        return FakeServer::handshake_reply(argv);
    });
    std::atomic<uint16_t> port_a{0};
    uint16_t pb = b.port();
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_a}});
        }
        if (argv[0] == "GET") {
            return redirect("ASK", hotkv::key_slot("k"), pb);
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();

    ClusterClient cluster(options_for(a.port()));
    EXPECT_EQ(cluster.command({"GET", "k"}).as_string(), "migrated");

    // B received ASKING immediately followed by GET on the same connection
    // (after the HELLO handshake), and nothing else.
    auto b_cmds = b.commands();
    ASSERT_GE(b_cmds.size(), 2u);
    ASSERT_EQ(b_cmds[b_cmds.size() - 2].argv[0], "ASKING");
    ASSERT_EQ(b_cmds[b_cmds.size() - 1].argv[0], "GET");
    EXPECT_EQ(b_cmds[b_cmds.size() - 2].connection, b_cmds[b_cmds.size() - 1].connection);
    EXPECT_EQ(count_named(b_cmds, "ASKING"), 1u);
    EXPECT_EQ(count_named(b_cmds, "GET"), 1u);

    // ASK must not touch the slot map: no refresh, and the next GET goes
    // to A first again.
    EXPECT_EQ(count_named(a.commands(), "CLUSTER"), 1u);
    EXPECT_EQ(cluster.command({"GET", "k"}).as_string(), "migrated");
    EXPECT_EQ(count_named(a.commands(), "GET"), 2u);
    EXPECT_EQ(count_named(a.commands(), "CLUSTER"), 1u);
}

TEST(ClusterRedirects, EndlessAskIsBoundedAndSurfacesAnError) {
    std::atomic<uint16_t> port_a{0};
    std::atomic<uint16_t> port_b{0};
    FakeServer b([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "ASKING") {
            return std::string("+OK\r\n");
        }
        if (argv[0] == "GET") {
            return redirect("ASK", hotkv::key_slot("k"), port_a);
        }
        return FakeServer::handshake_reply(argv);
    });
    port_b = b.port();
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_a}});
        }
        if (argv[0] == "ASKING") {
            return std::string("+OK\r\n");
        }
        if (argv[0] == "GET") {
            return redirect("ASK", hotkv::key_slot("k"), port_b);
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();

    ClusterClient cluster(options_for(a.port()));
    EXPECT_THROW(cluster.command({"GET", "k"}), ConnectionError);
    EXPECT_LE(count_named(a.commands(), "GET") + count_named(b.commands(), "GET"), 12u);
}

TEST(ClusterRedirects, EndlessMovedIsBoundedAndSurfacesAnError) {
    std::atomic<uint16_t> port_a{0};
    std::atomic<uint16_t> port_b{0};
    FakeServer b([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "GET") {
            return redirect("MOVED", hotkv::key_slot("k"), port_a);
        }
        return FakeServer::handshake_reply(argv);
    });
    port_b = b.port();
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_a}});
        }
        if (argv[0] == "GET") {
            return redirect("MOVED", hotkv::key_slot("k"), port_b);
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();

    ClusterClient cluster(options_for(a.port()));
    EXPECT_THROW(cluster.command({"GET", "k"}), ConnectionError);
    EXPECT_LE(count_named(a.commands(), "GET") + count_named(b.commands(), "GET"), 12u);
}

TEST(ClusterRedirects, TryAgainIsRetriedAfterABackoff) {
    std::atomic<uint16_t> port_a{0};
    std::atomic<int> gets{0};
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_a}});
        }
        if (argv[0] == "GET") {
            if (gets++ < 2) {
                return std::string("-TRYAGAIN Multiple keys request during rehashing of slot\r\n");
            }
            return bulk("ok");
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();

    ClusterClient cluster(options_for(a.port()));
    EXPECT_EQ(cluster.command({"GET", "k"}).as_string(), "ok");
    EXPECT_EQ(gets.load(), 3);
}

TEST(ClusterRedirects, ClusterDownRetriesAreBoundedThenTheServerErrorSurfaces) {
    std::atomic<uint16_t> port_a{0};
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_a}});
        }
        if (argv[0] == "GET") {
            return std::string("-CLUSTERDOWN The cluster is down\r\n");
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();

    ClusterClient cluster(options_for(a.port()));
    try {
        cluster.command({"GET", "k"});
        FAIL() << "expected a ServerError";
    } catch (const ServerError& e) {
        EXPECT_EQ(e.code(), "CLUSTERDOWN");
    }
    EXPECT_EQ(count_named(a.commands(), "GET"), 6u); // 1 try + 5 bounded retries
}

TEST(ClusterRedirects, OrdinaryServerErrorsAreNotRetried) {
    std::atomic<uint16_t> port_a{0};
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_a}});
        }
        if (argv[0] == "GET") {
            return std::string("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();
    ClusterClient cluster(options_for(a.port()));
    EXPECT_THROW(cluster.command({"GET", "k"}), ServerError);
    EXPECT_EQ(count_named(a.commands(), "GET"), 1u);
}

TEST(ClusterRedirects, MovedWithEmptyHostMeansTheNodeThatAnswered) {
    // "-MOVED slot :port" (unknown-endpoint): same host as the node asked.
    std::atomic<uint16_t> port_a{0};
    FakeServer b([](int, const std::vector<std::string>& argv) {
        if (argv[0] == "GET") {
            return bulk("b");
        }
        return FakeServer::handshake_reply(argv);
    });
    uint16_t pb = b.port();
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_a}});
        }
        if (argv[0] == "GET") {
            return "-MOVED " + std::to_string(hotkv::key_slot("k")) + " :" + std::to_string(pb) + "\r\n";
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();
    ClusterClient cluster(options_for(a.port()));
    EXPECT_EQ(cluster.command({"GET", "k"}).as_string(), "b");
}

TEST(ClusterRedirects, RoutesByTheRealKeyForEvalAndXread) {
    // foo hashes to 12182 -> the second node of a two way split.
    ASSERT_EQ(hotkv::key_slot("foo"), 12182);
    std::atomic<uint16_t> port_a{0};
    std::atomic<uint16_t> port_b{0};
    auto ranges = [&] { return slots_reply({{0, 8191, port_a}, {8192, 16383, port_b}}); };
    FakeServer b([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return ranges();
        }
        if (argv[0] == "EVAL" || argv[0] == "XREAD") {
            return std::string(":7\r\n");
        }
        return FakeServer::handshake_reply(argv);
    });
    port_b = b.port();
    FakeServer a([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return ranges();
        }
        return FakeServer::handshake_reply(argv);
    });
    port_a = a.port();

    ClusterClient cluster(options_for(a.port()));
    EXPECT_EQ(cluster.command({"EVAL", "return 1", "1", "foo"}).as_int(), 7);
    EXPECT_EQ(cluster.command({"XREAD", "COUNT", "1", "STREAMS", "foo", "0"}).as_int(), 7);
    EXPECT_EQ(count_named(b.commands(), "EVAL"), 1u);
    EXPECT_EQ(count_named(b.commands(), "XREAD"), 1u);
    EXPECT_EQ(count_named(a.commands(), "EVAL"), 0u);
    EXPECT_EQ(count_named(a.commands(), "XREAD"), 0u);

    // A keyless command goes to a known node without a hash-slot detour.
    EXPECT_NO_THROW(cluster.command({"PING", "x"}));
}

TEST(ClusterRedirects, SlotMapRefreshFallsBackToOtherNodesWhenTheSeedIsGone) {
    std::atomic<uint16_t> port_b{0};
    FakeServer b([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_b}});
        }
        if (argv[0] == "GET") {
            return bulk("from-b");
        }
        return FakeServer::handshake_reply(argv);
    });
    port_b = b.port();
    auto a = std::make_unique<FakeServer>([&](int, const std::vector<std::string>& argv) {
        if (argv[0] == "CLUSTER") {
            return slots_reply({{0, 16383, port_b}});
        }
        return FakeServer::handshake_reply(argv);
    });
    ClusterClient cluster(options_for(a->port()));
    cluster.refresh_slots(); // learns that B owns everything
    a.reset();                // the seed node dies
    EXPECT_NO_THROW(cluster.refresh_slots());
    EXPECT_EQ(cluster.command({"GET", "k"}).as_string(), "from-b");
}

#endif // !_WIN32
