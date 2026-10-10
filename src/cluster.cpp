// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/cluster.hpp>

#include <hotkv/error.hpp>
#include <hotkv/pipeline.hpp>

#include "client_impl.hpp"
#include "pairs_util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace hotkv {

namespace detail {

namespace {

// Upper bound on MOVED/ASK hops for one command (clients typically use 5 to
// 16; a healthy cluster needs at most 2).
constexpr int kMaxRedirects = 8;
// Upper bound on waits for a transient condition (TRYAGAIN, CLUSTERDOWN,
// LOADING, MASTERDOWN) before the error is handed to the caller.
constexpr int kMaxTransientRetries = 5;
constexpr std::size_t kSlotCount = 16384;

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool is_number(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// The argument a command is routed by, or nullptr for commands that
// carry no key (they go to any node). Covers the common cases where the
// first key is not args[1]; anything else is routed by args[1], and
// ClusterClient::command_for_key() is the explicit override.
const std::string* routing_key(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return nullptr;
    }
    static const std::unordered_set<std::string> keyless = {
        "PING", "ECHO", "INFO", "DBSIZE", "KEYS", "SCAN", "RANDOMKEY", "TIME", "FLUSHALL", "FLUSHDB", "CLUSTER",
        "CLIENT", "CONFIG", "SCRIPT", "COMMAND", "SLOWLOG", "LASTSAVE", "SAVE", "BGSAVE", "BGREWRITEAOF", "ROLE",
        "HELLO", "AUTH", "SELECT", "READONLY", "READWRITE", "ASKING", "WAIT", "PUBLISH", "PUBSUB", "LATENCY",
        "MODULE", "ACL", "OBSERVE", "ALERTS", "HOTKEYS",
    };
    const std::string name = upper(args[0]);
    if (keyless.count(name) != 0) {
        return nullptr;
    }
    // <cmd> <script|sha> <numkeys> [key ...]
    if (name == "EVAL" || name == "EVALSHA" || name == "EVAL_RO" || name == "EVALSHA_RO" || name == "FCALL" ||
        name == "FCALL_RO") {
        if (args.size() >= 4 && is_number(args[2]) && args[2] != "0") {
            return &args[3];
        }
        return nullptr;
    }
    // <cmd> <numkeys> <key> ...
    if (name == "ZUNION" || name == "ZINTER" || name == "ZDIFF" || name == "ZINTERCARD" || name == "SINTERCARD" ||
        name == "LMPOP" || name == "ZMPOP") {
        return args.size() >= 3 ? &args[2] : nullptr;
    }
    // <cmd> <timeout> <numkeys> <key> ...
    if (name == "BLMPOP" || name == "BZMPOP") {
        return args.size() >= 4 ? &args[3] : nullptr;
    }
    // XREAD [COUNT n] [BLOCK ms] STREAMS key ... id ...
    if (name == "XREAD" || name == "XREADGROUP") {
        for (std::size_t i = 1; i + 1 < args.size(); ++i) {
            if (upper(args[i]) == "STREAMS") {
                return &args[i + 1];
            }
        }
        return nullptr;
    }
    // <cmd> <subcommand> <key>
    if (name == "OBJECT" || name == "XINFO" || name == "XGROUP" || name == "MEMORY" || name == "BITOP") {
        return args.size() >= 3 ? &args[2] : nullptr;
    }
    return &args[1];
}

} // namespace

struct NodeEntry {
    std::string host;
    int port = 0;
    Client client;
};

class ClusterClientImpl {
public:
    explicit ClusterClientImpl(Options seed_options)
        : seed_options_(seed_options), seed_client_(seed_options) {}

    // Asks the seed node first and then every node learned so far, so a
    // dead seed (after a failover, say) does not stop the map from being
    // refreshed. Throws the last error if no node could answer.
    void refresh_slots() {
        std::vector<Client> candidates;
        candidates.push_back(seed_client_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const NodeEntry& n : nodes_) {
                candidates.push_back(n.client);
            }
        }
        std::exception_ptr last_error;
        for (Client& node : candidates) {
            try {
                fetch_and_apply(node);
                return;
            } catch (const ServerError&) {
                // A node that answers with an error (not in cluster mode,
                // say) may still be followed by one that can answer.
                last_error = std::current_exception();
            } catch (const ConnectionError&) {
                last_error = std::current_exception();
            }
        }
        if (last_error) {
            std::rethrow_exception(last_error);
        }
    }

    void set_read_from_replicas(bool enabled) { read_from_replicas_ = enabled; }

    Reply command(std::vector<std::string> args) {
        if (const std::string* key = routing_key(args)) {
            std::string k = *key;
            return command_for_key(k, std::move(args));
        }
        ensure_slots_loaded();
        Client node = any_known_node();
        return node.command(std::move(args));
    }

    // Redirect and retry policy, per command:
    //   MOVED   the slot has a new owner: fetch the slot map again (best
    //           effort), pin the slot to the owner the error names, and
    //           retry there. Counts against kMaxRedirects.
    //   ASK     the slot is mid-migration and this key is already on the
    //           target: send ASKING then the command to the target, once,
    //           on one connection, and leave the slot map alone. A second
    //           ASK for the same command is followed again (bounded by
    //           kMaxRedirects).
    //   TRYAGAIN / CLUSTERDOWN / LOADING / MASTERDOWN
    //           transient: back off and retry, at most
    //           kMaxTransientRetries times, then rethrow the server error.
    //           CLUSTERDOWN and MASTERDOWN also refresh the slot map.
    //   ConnectionError on an idempotent read: the node may be gone;
    //           refresh the slot map and retry once. Other commands are
    //           never retried (the write may have been applied), but the
    //           slot map is marked stale so the next command refreshes it.
    Reply command_for_key(std::string_view key, std::vector<std::string> args) {
        ensure_slots_loaded();
        const uint16_t slot = key_slot(key);
        const bool read_only = is_idempotent_read(args);

        int redirects = 0;
        int transient = 0;
        bool conn_retried = false;
        bool force_primary = false;
        std::optional<Client> ask_node;

        for (;;) {
            const bool asking = ask_node.has_value();
            const bool via_replica = !asking && read_from_replicas_ && read_only && !force_primary;
            Client node = asking ? *ask_node : pick_client_for_slot(slot, via_replica);
            ask_node.reset();
            try {
                if (asking) {
                    return send_with_prefix(node, "ASKING", args);
                }
                if (via_replica) {
                    return send_with_prefix(node, "READONLY", args);
                }
                return node.command(args);
            } catch (const MovedError& e) {
                if (++redirects > kMaxRedirects) {
                    throw ConnectionError("hotkv: too many cluster redirects (>" + std::to_string(kMaxRedirects) +
                                          ")");
                }
                const std::string host = e.host().empty() ? node.options().host : e.host();
                const int port = e.port() > 0 ? e.port() : node.options().port;
                try {
                    refresh_slots();
                } catch (const Error&) {
                    // Keep going with the single-slot correction below.
                }
                update_single_slot_owner(slot, host, port);
                force_primary = true;
            } catch (const AskError& e) {
                if (++redirects > kMaxRedirects) {
                    throw ConnectionError("hotkv: too many cluster redirects (>" + std::to_string(kMaxRedirects) +
                                          ")");
                }
                const std::string host = e.host().empty() ? node.options().host : e.host();
                const int port = e.port() > 0 ? e.port() : node.options().port;
                ask_node = get_or_create_node(host, port);
            } catch (const ServerError& e) {
                const std::string& code = e.code();
                const bool transient_code =
                    code == "TRYAGAIN" || code == "CLUSTERDOWN" || code == "LOADING" || code == "MASTERDOWN";
                if (!transient_code || ++transient > kMaxTransientRetries) {
                    throw;
                }
                if (code == "CLUSTERDOWN" || code == "MASTERDOWN") {
                    try {
                        refresh_slots();
                    } catch (const Error&) {
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5 * (1 << (transient - 1))));
            } catch (const ConnectionError&) {
                mark_stale();
                if (!read_only || conn_retried) {
                    throw;
                }
                conn_retried = true;
                force_primary = true;
                try {
                    refresh_slots();
                } catch (const Error&) {
                }
            }
        }
    }

private:
    void fetch_and_apply(Client& node) {
        Reply r;
        bool used_shards = false;
        try {
            r = node.command({"CLUSTER", "SLOTS"});
        } catch (const ServerError&) {
            r = node.command({"CLUSTER", "SHARDS"});
            used_shards = true;
        }
        if (used_shards) {
            apply_cluster_shards(r);
        } else {
            apply_cluster_slots(r);
        }
    }

    // Sends `prefix_command` then `args` on the same borrowed connection
    // (via Pipeline), returning args' reply. Used for ASKING and
    // READONLY, both of which apply only to the very next command on
    // that connection.
    static Reply send_with_prefix(Client& node, const char* prefix_command, const std::vector<std::string>& args) {
        Pipeline p = node.pipeline();
        p.command({prefix_command});
        p.command(args);
        std::vector<Reply> replies = p.exec();
        if (replies.size() != 2) {
            throw ProtocolError("hotkv: malformed cluster redirect response");
        }
        // Only the command's own reply is surfaced; if the prefix itself
        // was refused the command reply (MOVED/ASK/...) says so anyway.
        replies[1].raise_if_error();
        return replies[1];
    }

    void mark_stale() {
        std::lock_guard<std::mutex> lock(mutex_);
        loaded_ = false;
    }

    void ensure_slots_loaded() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (loaded_) {
                return;
            }
        }
        refresh_slots();
    }

    Client any_known_node() {
        std::lock_guard<std::mutex> lock(mutex_);
        return nodes_.empty() ? seed_client_ : nodes_.front().client;
    }

    Client pick_client_for_slot(uint16_t slot, bool prefer_replica) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (prefer_replica && slot < slot_to_replica_node_.size() && slot_to_replica_node_[slot] >= 0) {
            return nodes_[static_cast<std::size_t>(slot_to_replica_node_[slot])].client;
        }
        if (slot < slot_to_node_.size() && slot_to_node_[slot] >= 0) {
            return nodes_[static_cast<std::size_t>(slot_to_node_[slot])].client;
        }
        // Unknown owner: fall back to the seed node, which will likely
        // reply MOVED and let the next attempt correct the map.
        return seed_client_;
    }

    Client get_or_create_node(const std::string& host, int port) {
        std::lock_guard<std::mutex> lock(mutex_);
        return nodes_[get_or_create_node_index_locked(host, port)].client;
    }

    std::size_t get_or_create_node_index_locked(const std::string& raw_host, int port) {
        // Some deployments advertise an empty or "?" endpoint for a node
        // (cluster-preferred-endpoint-type unknown-endpoint): it means
        // "the address you used to reach me", i.e. the seed's host.
        const std::string host = (raw_host.empty() || raw_host == "?") ? seed_options_.host : raw_host;
        std::string addr = host + ":" + std::to_string(port);
        auto it = node_index_by_addr_.find(addr);
        if (it != node_index_by_addr_.end()) {
            return it->second;
        }
        Options node_opts = seed_options_;
        node_opts.host = host;
        node_opts.port = static_cast<uint16_t>(port);
        nodes_.push_back(NodeEntry{host, port, Client(node_opts)});
        std::size_t idx = nodes_.size() - 1;
        node_index_by_addr_.emplace(std::move(addr), idx);
        return idx;
    }

    void update_single_slot_owner(uint16_t slot, const std::string& host, int port) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t idx = get_or_create_node_index_locked(host, port);
        if (slot_to_node_.size() != kSlotCount) {
            slot_to_node_.assign(kSlotCount, -1);
        }
        slot_to_node_[slot] = static_cast<int32_t>(idx);
        loaded_ = true;
    }

    void apply_cluster_slots(const Reply& r) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<int32_t> new_masters(kSlotCount, -1);
        std::vector<int32_t> new_replicas(kSlotCount, -1);

        for (const Reply& item : r.as_array()) {
            const Reply::Array& range = item.as_array();
            if (range.size() < 3) {
                continue;
            }
            int64_t start = range[0].as_int();
            int64_t end = range[1].as_int();
            const Reply::Array& master = range[2].as_array();
            if (master.size() < 2) {
                continue;
            }
            std::size_t master_idx = get_or_create_node_index_locked(
                master[0].is_nil() ? std::string() : master[0].as_string(), static_cast<int>(master[1].as_int()));

            std::size_t replica_idx = static_cast<std::size_t>(-1);
            bool has_replica = false;
            if (range.size() >= 4) {
                const Reply::Array& replica = range[3].as_array();
                if (replica.size() >= 2) {
                    replica_idx = get_or_create_node_index_locked(
                        replica[0].is_nil() ? std::string() : replica[0].as_string(),
                        static_cast<int>(replica[1].as_int()));
                    has_replica = true;
                }
            }

            for (int64_t slot = start; slot >= 0 && slot < static_cast<int64_t>(kSlotCount) && slot <= end;
                 ++slot) {
                new_masters[static_cast<std::size_t>(slot)] = static_cast<int32_t>(master_idx);
                if (has_replica) {
                    new_replicas[static_cast<std::size_t>(slot)] = static_cast<int32_t>(replica_idx);
                }
            }
        }

        slot_to_node_ = std::move(new_masters);
        slot_to_replica_node_ = std::move(new_replicas);
        loaded_ = true;
    }

    // Best-effort CLUSTER SHARDS parser, used only when CLUSTER SLOTS is
    // unavailable. Each shard entry is a flat map/array with "slots"
    // (flat pairs of [start, end, start, end, ...]) and "nodes" (array
    // of per-node maps/arrays with "ip", "port" and "role").
    void apply_cluster_shards(const Reply& r) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<int32_t> new_masters(kSlotCount, -1);
        std::vector<int32_t> new_replicas(kSlotCount, -1);

        for (const Reply& shard : r.as_array()) {
            auto shard_fields = shard.as_pairs();
            const Reply* slots_reply = pairs_find(shard_fields, "slots");
            const Reply* nodes_reply = pairs_find(shard_fields, "nodes");
            if (slots_reply == nullptr || nodes_reply == nullptr) {
                continue;
            }

            std::vector<int64_t> slot_bounds;
            for (const Reply& v : slots_reply->as_array()) {
                slot_bounds.push_back(v.as_int());
            }

            int32_t master_idx = -1;
            int32_t replica_idx = -1;
            for (const Reply& node : nodes_reply->as_array()) {
                auto node_fields = node.as_pairs();
                const Reply* ip = pairs_find(node_fields, "ip");
                const Reply* port = pairs_find(node_fields, "port");
                const Reply* role = pairs_find(node_fields, "role");
                if (ip == nullptr || port == nullptr) {
                    continue;
                }
                std::size_t idx = get_or_create_node_index_locked(ip->is_nil() ? std::string() : ip->as_string(),
                                                                    static_cast<int>(port->as_int()));
                std::string role_str = role != nullptr && !role->is_nil() ? role->as_string() : "master";
                if (role_str == "master" || role_str == "primary") {
                    master_idx = static_cast<int32_t>(idx);
                } else if (replica_idx < 0) {
                    replica_idx = static_cast<int32_t>(idx);
                }
            }
            if (master_idx < 0) {
                continue;
            }

            for (std::size_t i = 0; i + 1 < slot_bounds.size(); i += 2) {
                for (int64_t slot = slot_bounds[i]; slot >= 0 && slot < static_cast<int64_t>(kSlotCount) &&
                                                     slot <= slot_bounds[i + 1];
                     ++slot) {
                    new_masters[static_cast<std::size_t>(slot)] = master_idx;
                    if (replica_idx >= 0) {
                        new_replicas[static_cast<std::size_t>(slot)] = replica_idx;
                    }
                }
            }
        }

        slot_to_node_ = std::move(new_masters);
        slot_to_replica_node_ = std::move(new_replicas);
        loaded_ = true;
    }

    Options seed_options_;
    Client seed_client_;

    std::mutex mutex_;
    std::vector<NodeEntry> nodes_;
    std::unordered_map<std::string, std::size_t> node_index_by_addr_;
    std::vector<int32_t> slot_to_node_;
    std::vector<int32_t> slot_to_replica_node_;
    bool loaded_ = false;
    bool read_from_replicas_ = false;
};

} // namespace detail

ClusterClient::ClusterClient(Options seed_options)
    : impl_(std::make_shared<detail::ClusterClientImpl>(std::move(seed_options))) {}

ClusterClient ClusterClient::connect(const std::string& seed_url) {
    return ClusterClient(Options::parse_url(seed_url));
}

ClusterClient ClusterClient::connect(const Options& seed_options) {
    return ClusterClient(seed_options);
}

void ClusterClient::refresh_slots() {
    impl_->refresh_slots();
}

Reply ClusterClient::command(std::vector<std::string> args) {
    return impl_->command(std::move(args));
}

Reply ClusterClient::command_for_key(std::string_view key, std::vector<std::string> args) {
    return impl_->command_for_key(key, std::move(args));
}

void ClusterClient::set_read_from_replicas(bool enabled) {
    impl_->set_read_from_replicas(enabled);
}

} // namespace hotkv
