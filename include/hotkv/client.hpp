// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include <hotkv/detail/args.hpp>
#include <hotkv/error.hpp>
#include <hotkv/options.hpp>
#include <hotkv/reply.hpp>
#include <hotkv/scan.hpp>
#include <hotkv/types.hpp>

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace hotkv {

namespace detail {
class ClientImpl;
}

class Pipeline;
class Transaction;
class Subscriber;
class ClusterClient;

// HotKV exclusive + AI command group accessors; defined in
// <hotkv/enterprise.hpp>, included transitively by <hotkv/hotkv.hpp>.
class TagCommands;
class HotKeysCommands;
class GcraCommands;
class VsimCommands;
class SCacheCommands;
class PCacheCommands;
class KeyedCacheCommands;
class LlmStatsCommands;
class LlmBudgetCommands;
class PromptRegistryCommands;
class EmbedDedupCommands;
class AgentMemoryCommands;
class RagCommands;
class RateLimitCommands;
class TimeSeriesCommands;
class FeatureStoreCommands;
class ObserveCommands;
class AlertsCommands;

// A synchronous, thread-safe HotKV client backed by a bounded connection
// pool. Client is a small value type (a shared_ptr to the pool and
// options); copy it freely, share it across threads, pass it by value.
// Every method below may throw hotkv::ConnectionError, hotkv::TimeoutError
// or hotkv::ProtocolError on a transport failure, or the appropriate
// hotkv::ServerError subclass (see error.hpp) if the server rejects the
// command. Idempotent reads are retried once, transparently, after a
// connection loss; writes are never retried automatically.
class Client {
public:
    // Connects using a hotkv://, hotkvs://, redis:// or rediss:// URL
    // (see Options::parse_url). The pool is created lazily: no network
    // I/O happens until the first command.
    static Client connect(const std::string& url);
    static Client connect(const Options& options);
    explicit Client(Options options);

    Client(const Client&) = default;
    Client(Client&&) noexcept = default;
    Client& operator=(const Client&) = default;
    Client& operator=(Client&&) noexcept = default;
    ~Client() = default;

    const Options& options() const noexcept;

    // --- generic escape hatch ---

    // Sends any command and returns its raw reply. The first element of
    // `args` is the command name. A braced list of string literals
    // list-initializes `args` directly (e.g. command({"PING"})).
    Reply command(std::vector<std::string> args);

    // Convenience over command(): each argument is converted with
    // hotkv::detail::to_arg (strings pass through verbatim; integers use
    // std::to_string; floating point uses the locale-independent shortest
    // round-trip form, NaN throws InvalidArgumentError).
    // Example: client.execute("SET", "k", 42).
    template <typename... Args>
    Reply execute(Args&&... args) {
        std::vector<std::string> built;
        built.reserve(sizeof...(Args));
        (built.emplace_back(detail::to_arg(std::forward<Args>(args))), ...);
        return command(std::move(built));
    }

    // --- connection-oriented features ---

    // Queues commands and sends them in one round trip; see pipeline.hpp.
    Pipeline pipeline();
    // Begins a MULTI/EXEC transaction on a dedicated connection; see
    // transaction.hpp.
    Transaction transaction();
    // Opens a dedicated connection for SUBSCRIBE/PSUBSCRIBE/SSUBSCRIBE;
    // see subscriber.hpp.
    Subscriber create_subscriber();

    // --- server ---

    bool ping();
    std::string ping(std::string_view message);
    std::string echo(std::string_view message);
    std::string info(std::string_view section = {});
    int64_t dbsize();
    void flushdb(bool async = false);
    void flushall(bool async = false);
    ServerTime time();
    std::vector<std::pair<std::string, std::string>> config_get(std::string_view parameter);
    void config_set(std::string_view parameter, std::string_view value);
    void client_setname(std::string_view name);
    std::optional<std::string> client_getname();
    int64_t client_id();
    // Unix time of the last successful save.
    int64_t lastsave();
    // MEMORY USAGE: bytes used by the key's value, nullopt if no such key.
    std::optional<int64_t> memory_usage(std::string_view key);
    int64_t slowlog_len();
    void slowlog_reset();
    // SLOWLOG GET [count], returned raw (the entry shape is server-defined).
    Reply slowlog_get(int64_t count = 10);
    void config_resetstat();
    void swapdb(int db1, int db2);
    int64_t command_count();
    // CLUSTER introspection that also works on a non-cluster node (where
    // it describes a single standalone node). Cluster-aware routing is
    // hotkv::ClusterClient.
    std::string cluster_myid();
    std::string cluster_info();
    std::string cluster_nodes();
    Reply cluster_slots();
    int64_t cluster_keyslot(std::string_view key);
    int64_t cluster_countkeysinslot(int slot);
    std::vector<std::string> cluster_getkeysinslot(int slot, int64_t count);

    // --- pub/sub, publishing side (receiving is hotkv::Subscriber) ---

    // Returns the number of subscribers that received the message.
    int64_t publish(std::string_view channel, std::string_view message);
    int64_t spublish(std::string_view shard_channel, std::string_view message);
    // PUBSUB CHANNELS [pattern]: active channels (those with subscribers).
    std::vector<std::string> pubsub_channels(std::string_view pattern = {});
    // PUBSUB NUMSUB: (channel, subscriber count) in request order.
    std::vector<std::pair<std::string, int64_t>> pubsub_numsub(const std::vector<std::string>& channels);
    // PUBSUB NUMPAT: number of unique pattern subscriptions.
    int64_t pubsub_numpat();
    std::vector<std::string> pubsub_shardchannels(std::string_view pattern = {});
    std::vector<std::pair<std::string, int64_t>> pubsub_shardnumsub(const std::vector<std::string>& channels);

    // --- scripting ---

    Reply eval(std::string_view script, const std::vector<std::string>& keys, const std::vector<std::string>& args);
    Reply evalsha(std::string_view sha1_hex, const std::vector<std::string>& keys,
                  const std::vector<std::string>& args);
    // Computes sha1_hex(script) locally, tries EVALSHA, and falls back to
    // EVAL (which also primes the server's script cache) on NOSCRIPT.
    Reply eval_script(std::string_view script, const std::vector<std::string>& keys,
                       const std::vector<std::string>& args);
    // Read-only script variants (EVAL_RO / EVALSHA_RO).
    Reply eval_ro(std::string_view script, const std::vector<std::string>& keys,
                  const std::vector<std::string>& args);
    Reply evalsha_ro(std::string_view sha1_hex, const std::vector<std::string>& keys,
                     const std::vector<std::string>& args);
    std::string script_load(std::string_view script);
    std::vector<bool> script_exists(const std::vector<std::string>& sha1_hexes);
    void script_flush();

    // --- SCAN family (MATCH/COUNT/TYPE); see scan.hpp ---

    Scanner scan(const ScanOptions& opts = {});
    Scanner sscan(std::string_view key, const ScanOptions& opts = {});
    Scanner hscan(std::string_view key, const ScanOptions& opts = {});
    Scanner zscan(std::string_view key, const ScanOptions& opts = {});

    // --- strings ---

    std::optional<std::string> get(std::string_view key);
    // Returns whether the write happened (false only if NX/XX blocked it).
    bool set(std::string_view key, std::string_view value, const SetOptions& opts = {});
    // Like set(), but returns the previous value via SET ... GET (nullopt
    // if there was none; the write still happens unless NX/XX blocks it).
    std::optional<std::string> set_get(std::string_view key, std::string_view value, const SetOptions& opts = {});
    std::optional<std::string> getset(std::string_view key, std::string_view value);
    std::optional<std::string> getdel(std::string_view key);
    bool setnx(std::string_view key, std::string_view value);
    void setex(std::string_view key, int64_t seconds, std::string_view value);
    void psetex(std::string_view key, int64_t milliseconds, std::string_view value);
    int64_t append(std::string_view key, std::string_view value);
    int64_t strlen(std::string_view key);
    int64_t incr(std::string_view key);
    int64_t incrby(std::string_view key, int64_t delta);
    double incrbyfloat(std::string_view key, double delta);
    int64_t decr(std::string_view key);
    int64_t decrby(std::string_view key, int64_t delta);
    std::vector<std::optional<std::string>> mget(const std::vector<std::string>& keys);
    void mset(const std::vector<std::pair<std::string, std::string>>& kvs);
    bool msetnx(const std::vector<std::pair<std::string, std::string>>& kvs);
    std::string getrange(std::string_view key, int64_t start, int64_t end);
    int64_t setrange(std::string_view key, int64_t offset, std::string_view value);
    std::optional<std::string> getex(std::string_view key, const GetExOptions& opts = {});
    // Longest common subsequence of two string values (LCS key1 key2) and
    // its length (LCS ... LEN).
    std::string lcs(std::string_view key1, std::string_view key2);
    int64_t lcs_len(std::string_view key1, std::string_view key2);

    // --- generic / keys / expiry ---

    int64_t del(const std::vector<std::string>& keys);
    int64_t unlink(const std::vector<std::string>& keys);
    int64_t exists(const std::vector<std::string>& keys);
    std::string type(std::string_view key);
    bool expire(std::string_view key, int64_t seconds, ExpireCondition cond = ExpireCondition::None);
    bool pexpire(std::string_view key, int64_t milliseconds, ExpireCondition cond = ExpireCondition::None);
    bool expireat(std::string_view key, int64_t unix_seconds, ExpireCondition cond = ExpireCondition::None);
    bool pexpireat(std::string_view key, int64_t unix_milliseconds, ExpireCondition cond = ExpireCondition::None);
    int64_t ttl(std::string_view key);  // seconds; -2 no key, -1 no expiry
    int64_t pttl(std::string_view key); // milliseconds; -2 no key, -1 no expiry
    bool persist(std::string_view key);
    std::vector<std::string> keys(std::string_view pattern);
    std::optional<std::string> randomkey();
    bool rename(std::string_view src, std::string_view dst);
    bool renamenx(std::string_view src, std::string_view dst);
    int64_t touch(const std::vector<std::string>& keys);
    std::optional<std::string> dump(std::string_view key);
    void restore(std::string_view key, int64_t ttl_ms, const std::string& serialized, bool replace = false);
    bool copy(std::string_view src, std::string_view dst, std::optional<int> dest_db = std::nullopt,
              bool replace = false);
    bool move(std::string_view key, int db);
    std::optional<std::string> object_encoding(std::string_view key);
    // OBJECT FREQ / IDLETIME / REFCOUNT; nullopt when the key does not exist.
    std::optional<int64_t> object_freq(std::string_view key);
    std::optional<int64_t> object_idletime(std::string_view key);
    std::optional<int64_t> object_refcount(std::string_view key);
    // Absolute expiry as unix seconds / milliseconds; -1 no expiry, -2 no key.
    int64_t expiretime(std::string_view key);
    int64_t pexpiretime(std::string_view key);

    // Nanosecond-resolution TTL (HotKV core extension).
    bool nexpire(std::string_view key, int64_t nanoseconds);
    int64_t nttl(std::string_view key); // nanoseconds; -2 no key, -1 no expiry
    bool nexpireat(std::string_view key, int64_t unix_nanoseconds);

    // Hash field TTL (HEXPIRE family). Reply per field: 1 set, 0 condition
    // not met, 2 field deleted (HPERSIST/short TTL edge case), -2 no such
    // field/key. See the server's own HEXPIRE docs for exact codes.
    std::vector<int64_t> hexpire(std::string_view key, int64_t seconds, const std::vector<std::string>& fields,
                                  ExpireCondition cond = ExpireCondition::None);
    std::vector<int64_t> hpexpire(std::string_view key, int64_t milliseconds, const std::vector<std::string>& fields,
                                   ExpireCondition cond = ExpireCondition::None);
    std::vector<int64_t> hexpireat(std::string_view key, int64_t unix_seconds, const std::vector<std::string>& fields,
                                    ExpireCondition cond = ExpireCondition::None);
    std::vector<int64_t> hpexpireat(std::string_view key, int64_t unix_milliseconds,
                                     const std::vector<std::string>& fields,
                                     ExpireCondition cond = ExpireCondition::None);
    std::vector<int64_t> httl(std::string_view key, const std::vector<std::string>& fields);
    std::vector<int64_t> hpttl(std::string_view key, const std::vector<std::string>& fields);
    std::vector<int64_t> hpersist(std::string_view key, const std::vector<std::string>& fields);
    // Absolute per-field expiry (unix seconds / milliseconds); -1 no TTL, -2 no field.
    std::vector<int64_t> hexpiretime(std::string_view key, const std::vector<std::string>& fields);
    std::vector<int64_t> hpexpiretime(std::string_view key, const std::vector<std::string>& fields);

    // --- lists ---

    int64_t lpush(std::string_view key, const std::vector<std::string>& values);
    int64_t rpush(std::string_view key, const std::vector<std::string>& values);
    int64_t lpushx(std::string_view key, const std::vector<std::string>& values);
    int64_t rpushx(std::string_view key, const std::vector<std::string>& values);
    std::optional<std::string> lpop(std::string_view key);
    std::optional<std::string> rpop(std::string_view key);
    std::vector<std::string> lpop(std::string_view key, int64_t count);
    std::vector<std::string> rpop(std::string_view key, int64_t count);
    int64_t llen(std::string_view key);
    std::vector<std::string> lrange(std::string_view key, int64_t start, int64_t stop);
    void ltrim(std::string_view key, int64_t start, int64_t stop);
    std::optional<std::string> lindex(std::string_view key, int64_t index);
    bool lset(std::string_view key, int64_t index, std::string_view value);
    int64_t lrem(std::string_view key, int64_t count, std::string_view value);
    int64_t linsert_before(std::string_view key, std::string_view pivot, std::string_view value);
    int64_t linsert_after(std::string_view key, std::string_view pivot, std::string_view value);
    std::optional<std::string> lmove(std::string_view src, std::string_view dst, bool src_left, bool dst_left);
    std::optional<std::string> rpoplpush(std::string_view src, std::string_view dst);
    // Index of the first (or `rank`-th, negative counts from the tail)
    // match of `element`, or nullopt. maxlen 0 means unlimited.
    std::optional<int64_t> lpos(std::string_view key, std::string_view element,
                                 std::optional<int64_t> rank = std::nullopt, int64_t maxlen = 0);
    // LMPOP numkeys key... LEFT|RIGHT [COUNT n]: nullopt when every list is empty.
    std::optional<ListPopResult> lmpop(const std::vector<std::string>& keys, bool left, int64_t count = 1);

    // Blocking list commands. `timeout` 0 blocks until data arrives; the
    // call holds one pooled connection for the duration and the socket
    // timeout is extended past `timeout`, so a blocked call does not hit
    // Options::command_timeout. Return nullopt on timeout.
    std::optional<std::pair<std::string, std::string>> blpop(const std::vector<std::string>& keys,
                                                              std::chrono::milliseconds timeout);
    std::optional<std::pair<std::string, std::string>> brpop(const std::vector<std::string>& keys,
                                                              std::chrono::milliseconds timeout);
    std::optional<std::string> blmove(std::string_view src, std::string_view dst, bool src_left, bool dst_left,
                                       std::chrono::milliseconds timeout);
    std::optional<std::string> brpoplpush(std::string_view src, std::string_view dst,
                                           std::chrono::milliseconds timeout);
    std::optional<ListPopResult> blmpop(std::chrono::milliseconds timeout, const std::vector<std::string>& keys,
                                         bool left, int64_t count = 1);

    // --- sets ---

    int64_t sadd(std::string_view key, const std::vector<std::string>& members);
    int64_t srem(std::string_view key, const std::vector<std::string>& members);
    std::vector<std::string> smembers(std::string_view key);
    bool sismember(std::string_view key, std::string_view member);
    std::vector<bool> smismember(std::string_view key, const std::vector<std::string>& members);
    int64_t scard(std::string_view key);
    std::optional<std::string> spop(std::string_view key);
    std::vector<std::string> spop(std::string_view key, int64_t count);
    std::optional<std::string> srandmember(std::string_view key);
    std::vector<std::string> srandmember(std::string_view key, int64_t count);
    std::vector<std::string> sinter(const std::vector<std::string>& keys);
    int64_t sinterstore(std::string_view dst, const std::vector<std::string>& keys);
    int64_t sintercard(const std::vector<std::string>& keys, int64_t limit = 0);
    std::vector<std::string> sunion(const std::vector<std::string>& keys);
    int64_t sunionstore(std::string_view dst, const std::vector<std::string>& keys);
    std::vector<std::string> sdiff(const std::vector<std::string>& keys);
    int64_t sdiffstore(std::string_view dst, const std::vector<std::string>& keys);
    bool smove(std::string_view src, std::string_view dst, std::string_view member);

    // --- hashes ---

    bool hset(std::string_view key, std::string_view field, std::string_view value);
    int64_t hset(std::string_view key, const std::vector<std::pair<std::string, std::string>>& fields);
    std::optional<std::string> hget(std::string_view key, std::string_view field);
    // Newer hash commands (server 0.3.0 implements them).
    // HGETDEL key FIELDS n field...: returns each value (nullopt for a
    // missing field) and deletes the fields.
    std::vector<std::optional<std::string>> hgetdel(std::string_view key, const std::vector<std::string>& fields);
    // HGETEX key [EX|PX|EXAT|PXAT|PERSIST] FIELDS n field...
    std::vector<std::optional<std::string>> hgetex(std::string_view key, const std::vector<std::string>& fields,
                                                    const GetExOptions& opts = {});
    // HSETEX key [FNX|FXX] [EX|PX|EXAT|PXAT|KEEPTTL] FIELDS n field value...:
    // true if the fields were set, false if FNX/FXX blocked it.
    bool hsetex(std::string_view key, const std::vector<std::pair<std::string, std::string>>& fields,
                const HSetExOptions& opts = {});
    std::vector<std::optional<std::string>> hmget(std::string_view key, const std::vector<std::string>& fields);
    void hmset(std::string_view key, const std::vector<std::pair<std::string, std::string>>& fields);
    int64_t hdel(std::string_view key, const std::vector<std::string>& fields);
    bool hexists(std::string_view key, std::string_view field);
    int64_t hlen(std::string_view key);
    std::vector<std::string> hkeys(std::string_view key);
    std::vector<std::string> hvals(std::string_view key);
    std::vector<std::pair<std::string, std::string>> hgetall(std::string_view key);
    int64_t hincrby(std::string_view key, std::string_view field, int64_t delta);
    double hincrbyfloat(std::string_view key, std::string_view field, double delta);
    bool hsetnx(std::string_view key, std::string_view field, std::string_view value);
    int64_t hstrlen(std::string_view key, std::string_view field);
    std::vector<std::string> hrandfield(std::string_view key, int64_t count);
    std::vector<std::pair<std::string, std::string>> hrandfield_withvalues(std::string_view key, int64_t count);

    // --- sorted sets ---

    int64_t zadd(std::string_view key, const std::vector<ScoredMember>& members, const ZAddOptions& opts = {});
    std::optional<double> zadd_incr(std::string_view key, double delta, std::string_view member,
                                     const ZAddOptions& opts = {});
    int64_t zrem(std::string_view key, const std::vector<std::string>& members);
    std::optional<double> zscore(std::string_view key, std::string_view member);
    std::vector<std::optional<double>> zmscore(std::string_view key, const std::vector<std::string>& members);
    std::optional<int64_t> zrank(std::string_view key, std::string_view member);
    std::optional<int64_t> zrevrank(std::string_view key, std::string_view member);
    int64_t zcard(std::string_view key);
    int64_t zcount(std::string_view key, std::string_view min, std::string_view max);
    int64_t zlexcount(std::string_view key, std::string_view min, std::string_view max);
    double zincrby(std::string_view key, double delta, std::string_view member);
    // start/stop meaning depends on opts.by: plain integer indexes
    // (Index, default), score bounds like "1.5"/"(2"/"+inf" (Score), or
    // lex bounds like "[a"/"(z"/"-"/"+" (Lex).
    std::vector<std::string> zrange(std::string_view key, std::string_view start, std::string_view stop,
                                     const ZRangeOptions& opts = {});
    std::vector<ScoredMember> zrange_withscores(std::string_view key, std::string_view start, std::string_view stop,
                                                 const ZRangeOptions& opts = {});
    int64_t zrangestore(std::string_view dst, std::string_view src, std::string_view start, std::string_view stop,
                         const ZRangeOptions& opts = {});
    std::vector<std::string> zrangebyscore(std::string_view key, std::string_view min, std::string_view max,
                                            std::optional<int64_t> limit_offset = std::nullopt,
                                            std::optional<int64_t> limit_count = std::nullopt);
    std::vector<ScoredMember> zrangebyscore_withscores(std::string_view key, std::string_view min,
                                                        std::string_view max,
                                                        std::optional<int64_t> limit_offset = std::nullopt,
                                                        std::optional<int64_t> limit_count = std::nullopt);
    std::vector<std::string> zrevrangebyscore(std::string_view key, std::string_view max, std::string_view min,
                                               std::optional<int64_t> limit_offset = std::nullopt,
                                               std::optional<int64_t> limit_count = std::nullopt);
    std::vector<ScoredMember> zrevrangebyscore_withscores(std::string_view key, std::string_view max,
                                                           std::string_view min,
                                                           std::optional<int64_t> limit_offset = std::nullopt,
                                                           std::optional<int64_t> limit_count = std::nullopt);
    std::vector<std::string> zrangebylex(std::string_view key, std::string_view min, std::string_view max,
                                          std::optional<int64_t> limit_offset = std::nullopt,
                                          std::optional<int64_t> limit_count = std::nullopt);
    std::vector<std::string> zrevrangebylex(std::string_view key, std::string_view max, std::string_view min,
                                             std::optional<int64_t> limit_offset = std::nullopt,
                                             std::optional<int64_t> limit_count = std::nullopt);
    int64_t zremrangebyrank(std::string_view key, int64_t start, int64_t stop);
    int64_t zremrangebyscore(std::string_view key, std::string_view min, std::string_view max);
    int64_t zremrangebylex(std::string_view key, std::string_view min, std::string_view max);
    std::vector<ScoredMember> zpopmin(std::string_view key, int64_t count = 1);
    std::vector<ScoredMember> zpopmax(std::string_view key, int64_t count = 1);
    // ZMPOP numkeys key... MIN|MAX [COUNT n]: nullopt when every set is empty.
    std::optional<ZSetPopResult> zmpop(const std::vector<std::string>& keys, bool min, int64_t count = 1);
    // Blocking variants; `timeout` 0 blocks until data arrives (see blpop).
    std::optional<ZSetBlockingPop> bzpopmin(const std::vector<std::string>& keys, std::chrono::milliseconds timeout);
    std::optional<ZSetBlockingPop> bzpopmax(const std::vector<std::string>& keys, std::chrono::milliseconds timeout);
    std::optional<ZSetPopResult> bzmpop(std::chrono::milliseconds timeout, const std::vector<std::string>& keys,
                                         bool min, int64_t count = 1);
    std::vector<std::string> zrandmember(std::string_view key, int64_t count = 1);
    std::vector<ScoredMember> zrandmember_withscores(std::string_view key, int64_t count = 1);
    int64_t zunionstore(std::string_view dst, const std::vector<std::string>& keys, const ZStoreOptions& opts = {});
    int64_t zinterstore(std::string_view dst, const std::vector<std::string>& keys, const ZStoreOptions& opts = {});
    int64_t zdiffstore(std::string_view dst, const std::vector<std::string>& keys);
    std::vector<std::string> zunion(const std::vector<std::string>& keys, const ZStoreOptions& opts = {});
    std::vector<ScoredMember> zunion_withscores(const std::vector<std::string>& keys, const ZStoreOptions& opts = {});
    std::vector<std::string> zinter(const std::vector<std::string>& keys, const ZStoreOptions& opts = {});
    std::vector<ScoredMember> zinter_withscores(const std::vector<std::string>& keys, const ZStoreOptions& opts = {});
    int64_t zintercard(const std::vector<std::string>& keys, int64_t limit = 0);
    std::vector<std::string> zdiff(const std::vector<std::string>& keys);
    std::vector<ScoredMember> zdiff_withscores(const std::vector<std::string>& keys);

    // --- streams ---

    std::string xadd(std::string_view key, const std::vector<std::pair<std::string, std::string>>& fields,
                      std::string_view id = "*");
    std::string xadd_maxlen(std::string_view key, int64_t maxlen, bool approx,
                             const std::vector<std::pair<std::string, std::string>>& fields,
                             std::string_view id = "*");
    int64_t xlen(std::string_view key);
    std::vector<StreamEntry> xrange(std::string_view key, std::string_view start, std::string_view end,
                                     int64_t count = 0);
    std::vector<StreamEntry> xrevrange(std::string_view key, std::string_view end, std::string_view start,
                                        int64_t count = 0);
    int64_t xdel(std::string_view key, const std::vector<std::string>& ids);
    int64_t xtrim_maxlen(std::string_view key, int64_t maxlen, bool approx = false);
    std::vector<XReadResult> xread(const std::vector<std::pair<std::string, std::string>>& streams,
                                    int64_t count = 0, std::optional<int64_t> block_ms = std::nullopt);
    std::vector<XReadResult> xreadgroup(std::string_view group, std::string_view consumer,
                                         const std::vector<std::pair<std::string, std::string>>& streams,
                                         int64_t count = 0, std::optional<int64_t> block_ms = std::nullopt,
                                         bool noack = false);
    void xgroup_create(std::string_view key, std::string_view group, std::string_view id = "$",
                        bool mkstream = false);
    bool xgroup_destroy(std::string_view key, std::string_view group);
    bool xgroup_createconsumer(std::string_view key, std::string_view group, std::string_view consumer);
    int64_t xgroup_delconsumer(std::string_view key, std::string_view group, std::string_view consumer);
    void xgroup_setid(std::string_view key, std::string_view group, std::string_view id);
    int64_t xack(std::string_view key, std::string_view group, const std::vector<std::string>& ids);
    XPendingSummary xpending(std::string_view key, std::string_view group);
    // Extended form: XPENDING key group [IDLE ms] start end count [consumer].
    std::vector<XPendingEntry> xpending_range(std::string_view key, std::string_view group, std::string_view start,
                                               std::string_view end, int64_t count,
                                               std::optional<std::string> consumer = std::nullopt,
                                               std::optional<int64_t> min_idle_ms = std::nullopt);
    std::vector<StreamEntry> xclaim(std::string_view key, std::string_view group, std::string_view consumer,
                                     int64_t min_idle_ms, const std::vector<std::string>& ids);
    XAutoclaimResult xautoclaim(std::string_view key, std::string_view group, std::string_view consumer,
                                 int64_t min_idle_ms, std::string_view start, int64_t count = 0);
    Reply xinfo_stream(std::string_view key);
    Reply xinfo_groups(std::string_view key);
    Reply xinfo_consumers(std::string_view key, std::string_view group);

    // --- geospatial ---

    // Each point is (longitude, latitude, member).
    int64_t geoadd(std::string_view key, const std::vector<std::tuple<double, double, std::string>>& points);
    std::optional<double> geodist(std::string_view key, std::string_view member1, std::string_view member2,
                                   GeoUnit unit = GeoUnit::Meters);
    std::vector<std::optional<GeoPoint>> geopos(std::string_view key, const std::vector<std::string>& members);
    std::vector<std::optional<std::string>> geohash(std::string_view key, const std::vector<std::string>& members);
    std::vector<GeoSearchResult> geosearch(std::string_view key, const GeoSearchOptions& opts);
    int64_t geosearchstore(std::string_view dst, std::string_view src, const GeoSearchOptions& opts);

    // --- bitmaps ---

    int64_t setbit(std::string_view key, int64_t offset, int value);
    int64_t getbit(std::string_view key, int64_t offset);
    int64_t bitcount(std::string_view key);
    int64_t bitcount(std::string_view key, int64_t start, int64_t end, bool by_bit = false);
    int64_t bitpos(std::string_view key, int bit, std::optional<int64_t> start = std::nullopt,
                    std::optional<int64_t> end = std::nullopt, bool by_bit = false);
    int64_t bitop_and(std::string_view dst, const std::vector<std::string>& keys);
    int64_t bitop_or(std::string_view dst, const std::vector<std::string>& keys);
    int64_t bitop_xor(std::string_view dst, const std::vector<std::string>& keys);
    int64_t bitop_not(std::string_view dst, std::string_view key);
    // BITFIELD_RO: only GET sub-commands are allowed (usable on replicas).
    std::vector<std::optional<int64_t>> bitfield_ro(std::string_view key, const std::vector<std::string>& ops);
    // Each entry of `ops` is one BITFIELD sub-command token, e.g. "GET",
    // "u8", "0" flattened as three ops, or pre-joined however you like
    // ("INCRBY i5 100 10"); each GET/SET/INCRBY yields one reply element
    // (nullopt only for a failed OVERFLOW FAIL).
    std::vector<std::optional<int64_t>> bitfield(std::string_view key, const std::vector<std::string>& ops);

    // --- HyperLogLog ---

    bool pfadd(std::string_view key, const std::vector<std::string>& elements);
    void pfmerge(std::string_view dst, const std::vector<std::string>& sources);
    int64_t pfcount(const std::vector<std::string>& keys);

    // --- HotKV exclusive and AI command groups ---
    // Each accessor is a lightweight value; call again any time rather
    // than storing it, though storing is fine too (it just references
    // this Client's pool). See <hotkv/enterprise.hpp>.

    TagCommands tag();
    HotKeysCommands hotkeys();
    GcraCommands gcra();
    VsimCommands vsim();
    SCacheCommands scache();
    PCacheCommands pcache();
    KeyedCacheCommands tool_cache();
    KeyedCacheCommands ncache();
    KeyedCacheCommands guard();
    KeyedCacheCommands rerank();
    LlmStatsCommands llmstats();
    LlmBudgetCommands llmbudget();
    PromptRegistryCommands prompt();
    EmbedDedupCommands embed_dedup();
    AgentMemoryCommands amem();
    RagCommands rag();
    RateLimitCommands ratelimit();
    TimeSeriesCommands ts();
    FeatureStoreCommands feature();
    ObserveCommands observe();
    AlertsCommands alerts();

private:
    friend class Pipeline;
    friend class Transaction;
    friend class Subscriber;
    friend class ClusterClient;
    explicit Client(std::shared_ptr<detail::ClientImpl> impl);

    std::shared_ptr<detail::ClientImpl> impl_;
};

} // namespace hotkv
