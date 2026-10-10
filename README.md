# HotKV C++ Client

[![CI](https://github.com/hotkv-ltd/hotkv-cpp-client/actions/workflows/ci.yml/badge.svg)](https://github.com/hotkv-ltd/hotkv-cpp-client/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

A high-performance C++ client for [HotKV](https://hotkv.com), a RESP2/RESP3-compatible
in-memory data store with built-in AI and LLM-serving features (semantic and prompt
caching, agent memory, RAG, feature store, rate limiting, time series and more).

- Public headers in `include/hotkv/` backed by a compiled library (`hotkv-cpp`, static or shared)
- C++20, no required third-party runtime dependency (TLS is optional, via OpenSSL)
- Synchronous, thread-safe `Client` with a bounded connection pool
- Pipelines, transactions (`MULTI`/`EXEC`/`WATCH`), pub/sub, cluster routing
- Typed methods for the common command families and for the HotKV
  exclusive, AI and enterprise families; every other command through the generic
  `command()` call. "What is typed and what is not" below lists which (397 of
  605 commands of server 0.3.0 have a typed method)
- `PromptCache` and `SemanticCache` helpers for LLM response caching

Targets HotKV server 0.3.0 (the version it is tested against).

## Install

hotkv-cpp is a CMake package. Add it as a subdirectory or fetch it (with
`FetchContent`/`ExternalProject`), then:

```cmake
find_package(hotkv-cpp CONFIG REQUIRED)   # if installed system-wide / via a package manager

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE hotkv::hotkv-cpp)
```

Or build and install it yourself:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /path/to/prefix
```

A pkg-config file (`hotkv-cpp.pc`) is also installed, for non-CMake build systems
(`pkg-config --cflags --libs hotkv-cpp`, add `--static` for a static library). The
install tree is relocatable. Release archives (static and shared, Linux, macOS,
Windows) are built by `.github/workflows/publish.yml`; an overlay vcpkg port is in
`ports/hotkv-cpp-client` (use it with `--overlay-ports`; its `tls` feature, on by
default, pulls in OpenSSL).

Requirements: CMake 3.20 or newer and a C++20 compiler (gcc 13, clang 16 or newer, or
MSVC 2022). With TLS on, OpenSSL is a private implementation detail: no public header
includes it, and `find_package(hotkv-cpp)` resolves it for static consumers.
`scripts/verify-install.sh <workdir> [cmake args]` builds, installs, moves the install
tree and compiles `tests/consumer` against it through both `find_package` and
pkg-config. Windows release packages are built without TLS (the runner has no OpenSSL);
for TLS on Windows, install OpenSSL (for example through vcpkg) and configure with its
toolchain file.

### CMake options

| Option | Default | Purpose |
|---|---|---|
| `HOTKV_CPP_WITH_TLS` | `ON` (if OpenSSL is found) | Build with TLS support |
| `HOTKV_CPP_BUILD_TESTS` | `ON` when top-level | Build the unit/integration test binaries |
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library instead of a static one |

## Quick start

```cpp
#include <hotkv/hotkv.hpp>
#include <iostream>

int main() {
    hotkv::Client client = hotkv::Client::connect("hotkv://127.0.0.1:6379");

    client.set("greeting", "hello, hotkv");
    std::optional<std::string> value = client.get("greeting");
    if (value) {
        std::cout << *value << "\n";
    }

    client.expire("greeting", 60);
    std::cout << "ttl: " << client.ttl("greeting") << "\n";
}
```

`hotkv::Client` is a small, cheaply-copyable handle around a shared connection
pool: copy it freely and use it from multiple threads.

## Connection options

```cpp
hotkv::Options opts;
opts.host = "cache.internal";
opts.port = 6379;
opts.username = "app";
opts.password = std::getenv("HOTKV_PASSWORD");
opts.database = 2;
opts.client_name = "checkout-service";
opts.connect_timeout = std::chrono::milliseconds(3000);
opts.command_timeout = std::chrono::milliseconds(2000);
opts.pool_size = 16;

hotkv::Client client(opts);
```

Or parse a URL directly (`hotkv://`, `hotkvs://` for TLS, and `redis://`/`rediss://`
are accepted too, for drop-in migration):

```cpp
auto client = hotkv::Client::connect(
    "hotkv://app:secret@cache.internal:6379/2"
    "?client_name=checkout-service&pool_size=16&connect_timeout_ms=3000");
```

### TLS

```cpp
hotkv::Options opts;
opts.host = "cache.example.com";
opts.port = 6400;
opts.tls.enabled = true;
opts.tls.ca_file = "/etc/ssl/certs/ca-bundle.pem";   // empty uses the platform trust store
opts.tls.cert_file = "/etc/hotkv/client.pem";         // optional, for mutual TLS
opts.tls.key_file = "/etc/hotkv/client-key.pem";
// opts.tls.insecure_skip_verify = true;              // testing only

hotkv::Client client(opts);
```

TLS support requires the SDK to have been built with `HOTKV_CPP_WITH_TLS` (the
default, when OpenSSL is found); enabling it on a build without TLS support throws
`hotkv::ConnectionError` at connect time.

### Connection pooling

Every `Client` owns a bounded pool of physical connections (`Options::pool_size`,
default 8). Connections are created lazily, health-checked with `PING` after being
idle past `Options::pool_health_check_after`, and closed after
`Options::pool_idle_timeout`. Borrowing blocks (up to `connect_timeout`) when the
pool is exhausted, and reads recognized as idempotent (`GET`, `HGETALL`, `SCAN`, ...)
are retried once, on a fresh connection, after a connection loss; writes are never
retried automatically.

## Typed commands and the escape hatch

Common command families are typed methods directly on `Client`:

```cpp
client.set("k", "v");
client.lpush("mylist", {"a", "b", "c"});
client.hset("myhash", {{"field1", "v1"}, {"field2", "v2"}});
client.zadd("leaderboard", {{"alice", 100}, {"bob", 87}});
client.sadd("tags", {"red", "blue"});
client.xadd("events", {{"type", "click"}});
```

Anything not covered by a typed method is one call away via the generic escape
hatch, which returns a `hotkv::Reply` (a variant covering every RESP2/RESP3 type,
with typed accessors `as_int()`, `as_string()`, `as_array()`, `as_pairs()`, ...):

```cpp
hotkv::Reply r = client.command({"BF.ADD", "filter", "item"});
int64_t added = r.as_int();

// execute() converts arguments for you; floating point values use the same
// locale-independent formatting as the typed methods.
client.execute("SET", "k", 42);
```

### What is typed and what is not

Typed: strings,
keys and expiry (including `NEXPIRE`/hash field TTLs), lists (including the
blocking pops and `LMPOP`), sets, hashes (including the newer `HGETDEL`,
`HGETEX`, `HSETEX`), sorted sets (including `ZMPOP` and the blocking pops),
streams and consumer groups, geo (`GEOSEARCH`), bitmaps, HyperLogLog, scripting
(`EVAL`/`EVALSHA` and their `_RO` forms), SCAN, the publishing side of pub/sub,
and the basic server and `CLUSTER` introspection commands; `TAG`, `GCRA`, `VSIM`,
`HOTKEYS`; the AI and enterprise families `SCACHE`, `PCACHE`, `TOOL.CACHE`,
`NCACHE`, `GUARD`, `RERANK`, `LLMSTATS`, `LLMBUDGET`, `PROMPT`, `EMBED.DEDUP`, `AMEM`,
`RAG`, `RATELIMIT`, `TS`, `FEATURE`, `OBSERVE`, `ALERTS`. `MULTI`/`EXEC`/
`WATCH` are `Transaction`, `SUBSCRIBE` and friends are `Subscriber`.

Not typed, reachable with `command()`: the probabilistic structures `BF.*`,
`CMS.*` and `TOPK.*`; `TENANT.*`, `IAM.*`, `AUDIT.*`, `ENCRYPTION.*` and
`REPLICATION.*`; `CLIENT` subcommands beyond `SETNAME`/`GETNAME`/`ID` (including
`CLIENT TRACKING`, `CACHING`, `GETREDIR`, `TRACKINGINFO`, `KILL`, `LIST`); `ACL`,
`FUNCTION`/`FCALL`, `LATENCY`, `MODULE` and the remaining `MEMORY`, `SLOWLOG` and
`COMMAND` subcommands; cluster administration (`CLUSTER SETSLOT`, `MEET`,
`FAILOVER`, ..., `CLUSTERSCAN`, `TRIMSLOTS`, `SFLUSH`) and replication/persistence
commands (`REPLICAOF`, `SAVE`, `BGSAVE`, ...); `SORT`, `GEORADIUS*`, `ZREVRANGE`,
`MIGRATE`; and the newer commands `MSETEX`, `DELEX`, `DIGEST`, `XCFGSET`,
`XSETID`, `HIMPORT`, `FEATURE.SETAT`. Nothing is hidden: these all work through
`client.command({...})`, they just return a raw `Reply`.

### SCAN family

```cpp
hotkv::ScanOptions opts;
opts.match = "user:*";
opts.count = 200;
hotkv::Scanner scanner = client.scan(opts);

std::string key;
while (scanner.next(key)) {
    // ...
}
```

`hscan`/`sscan`/`zscan` work the same way (`hscan`/`zscan` yield pairs via
`next(a, b)` or, for `zscan`, `next_scored(member, score)`).

## Pipelines

Queue several commands and send them in a single round trip:

```cpp
hotkv::Pipeline p = client.pipeline();
p.command({"INCR", "a"});
p.command({"INCR", "b"});
p.command({"GET", "a"});

for (const hotkv::Reply& reply : p.exec()) {
    // A per-command server error surfaces as reply.is_error(), not as a
    // thrown exception, so one failing command never hides the others.
}
```

## Transactions

```cpp
hotkv::Transaction tx = client.transaction();
tx.watch({"balance"});                 // optional, before multi()
if (*client.get("balance") == "0") {
    tx.discard();
} else {
    tx.multi();
    tx.command({"DECRBY", "balance", "10"});
    tx.command({"INCRBY", "history", "1"});
    if (auto results = tx.exec()) {
        // committed; *results holds each queued command's reply
    } else {
        // aborted: a watched key changed since watch()
    }
}
```

## Pub/Sub

```cpp
hotkv::Subscriber sub = client.create_subscriber();
sub.subscribe({"orders"});
sub.psubscribe({"prices.*"});

// Pull style:
if (auto msg = sub.next_message(std::chrono::seconds(5))) {
    std::cout << msg->channel << ": " << msg->payload << "\n";
}

// Or callback style, on a background thread:
sub.listen([](const hotkv::Message& msg) {
    std::cout << msg->kind << " " << msg->channel << ": " << msg->payload << "\n";
});
// ... later
sub.stop();
```

`ssubscribe`/`sunsubscribe` (sharded pub/sub) work the same way. RESP3 push frames
and RESP2 array frames are both handled transparently on the same connection.

Publishing is typed on `Client`:

```cpp
int64_t receivers = client.publish("orders", "order 42 shipped");
client.spublish("shard-orders", "...");
auto active = client.pubsub_channels("orders*");
auto counts = client.pubsub_numsub({"orders"});   // vector of (channel, subscribers)
int64_t patterns = client.pubsub_numpat();
```

## Cluster

```cpp
hotkv::ClusterClient cluster = hotkv::ClusterClient::connect("hotkv://node1.internal:6379");

cluster.command_for_key("user:1000", {"SET", "user:1000", "..."});
cluster.command({"GET", "user:1000"});   // routes by args[1] automatically
```

`ClusterClient` builds its slot map from `CLUSTER SLOTS` (falling back to
`CLUSTER SHARDS`), computes the CRC16 hash slot for each key (honoring
`{hash tag}`s) and routes by it. Per command:

* `MOVED`: re-fetches the slot map, pins the slot to the node named in the error
  and retries there.
* `ASK`: sends `ASKING` and then the command to the named node on one connection,
  once per hop, and leaves the slot map unchanged.
* `TRYAGAIN`, `CLUSTERDOWN`, `LOADING`, `MASTERDOWN`: backs off and retries up to
  five times, then throws the server's error.
* A lost connection on a read refreshes the map and retries once; writes are never
  retried automatically.
* At most eight redirects per command, then `ConnectionError`.

The routing key is `args[1]` except for commands where it is elsewhere
(`EVAL`/`EVALSHA`/`FCALL` with keys, `XREAD`/`XREADGROUP`, `ZUNION` and the other
numkeys commands, `OBJECT`, `XINFO`, `BITOP`, ...); keyless commands go to any
node, and `command_for_key()` overrides the choice. Call
`cluster.set_read_from_replicas(true)` to route read commands to a replica via
`READONLY`; writes still go to the primary. A single command that touches several
slots is the server's `CROSSSLOT` error, as with any cluster client.

Redirect handling is covered by unit tests against scripted in-process servers
(`tests/unit/test_cluster_redirects.cpp`). It has not been run against a live
multi-node HotKV cluster.

## HotKV exclusive commands

The HotKV command groups are reached as accessor objects off `Client`:

```cpp
client.tag().set("product:42", {"electronics", "on-sale"});
auto matches = client.tag().members("on-sale");

auto limit = client.gcra().limit("api:user:1000", /*period_ms=*/1000, /*burst=*/10);
if (limit.denied) {
    // retry_after_ms tells the caller how long to back off
}

client.vsim().create_index("products", 384);
client.vsim().set("products", "sku-1", embedding_vector);
auto nearest = client.vsim().search("products", /*k=*/5, query_vector);
```

Other groups: `scache()`, `pcache()`, `tool_cache()`, `ncache()`, `guard()`,
`rerank()`, `llmstats()`, `llmbudget()`, `prompt()`, `embed_dedup()`, `amem()`
(with `.conv()`, `.sem()`, `.work()`, `.epis()`, `.summary()`), `rag()`,
`ratelimit()`, `ts()`, `feature()`, `observe()`, `alerts()`. Every
enterprise command needs a licensed server and throws `hotkv::NotLicensedError`
(a subclass of `hotkv::ServerError`) when the server is not licensed for it, so
callers can catch that specifically and degrade gracefully.

## AI caching

`hotkv::ai::PromptCache` wraps `PCACHE.*` with a deterministic, cross-language
cache key (see `hotkv::ai::build_prompt_cache_key`) so an LLM call only happens on
a cache miss. Reporting `model` on the returned `LlmUsage` attaches it to the
stored entry, so a later hit's tokens and cost show up in `cache.stats()`:

```cpp
#include <hotkv/ai.hpp>

hotkv::ai::PromptCache cache(client, "chat-cache", /*ttl_seconds=*/3600);
cache.create_namespace();
cache.with_stats_bucket("chat-llm-stats"); // optional: records hits/misses/cost via LLMSTATS

hotkv::ai::PromptCacheKeyInput input;
input.provider = "openai";
input.model = "gpt-4o";
input.messages = {{"user", "Summarize this contract in one paragraph."}};
input.params = {{"temperature", "0.0"}}; // caller formats numbers, so keys match across SDKs

auto result = cache.get_or_call(input, [&]() -> hotkv::ai::LlmCallResult {
    // This lambda runs only on a cache miss.
    auto response = my_llm_client.chat(input.model, input.messages);
    hotkv::ai::LlmUsage usage;
    usage.model = input.model;
    usage.input_tokens = response.input_tokens;
    usage.output_tokens = response.output_tokens;
    usage.cost_usd = response.cost_usd;
    usage.latency = response.latency;
    return {response.text, usage};
});

std::cout << (result.hit ? "cache hit: " : "cache miss: ") << result.response << "\n";
std::cout << "tokens saved so far: " << cache.stats().usage.input_tokens << "\n";
```

`hotkv::ai::SemanticCache` is the same idea over `SCACHE.*`, keyed by a
caller-supplied embedding vector instead of an exact prompt match, and returning
the same `LlmCallResult` shape:

```cpp
hotkv::ai::SemanticCache semantic(client, "faq-cache");
semantic.create_namespace(/*dims=*/1536, "cosine", /*threshold=*/0.85);

auto vector = my_embedding_client.embed(question);
auto result = semantic.get_or_call(question, vector, [&]() -> hotkv::ai::LlmCallResult {
    return {my_llm_client.answer(question), {}}; // only called on a miss
});
```

Neither helper depends on any particular LLM or embedding provider's SDK; they
only need a `std::function` that returns a response.

### Token usage, cache savings and LLM cost tracking

`PCacheCommands::set`, `SCacheCommands::set`, the keyed caches' `set`
(`client.tool_cache()`/`ncache()`/`guard()`/`rerank()`) and
`LlmBudgetCommands::consume` all take a trailing `std::optional<hotkv::Usage>`
(`model` required, token counts optional): recording the original call's token
usage lets a later cache hit attribute the tokens, and once the model is priced,
the cost, it did not have to spend. Their `stats(...)` replies
(`SCacheStats`/`PCacheStats`/`KeyedCacheStats`) expose those savings as a
`UsageSavings usage` field alongside the existing counters.

`LLMSTATS.RECORD` with a model prices a call from HotKV's built-in price table
instead of a caller supplied cost; `LlmStatsCommands::price_set` adds or
overrides a model's price, and `LlmBudgetCommands::consume` with a model
enforces a budget priced the same way:

```cpp
// Custom or fine-tuned model: 3 USD / 1M input tokens, 15 USD / 1M output.
client.llmstats().price_set("acme-custom-v1", 3, 15);

client.llmstats().bucket("chatbot-usage");
hotkv::LlmStatsRecordOptions opts;
opts.model = "acme-custom-v1";
client.llmstats().record("chatbot-usage", 500, 120, 0, 0, 340, opts);

auto usage = client.llmstats().get("chatbot-usage");
std::cout << "spent $" << usage.cost_usd << " so far\n";

hotkv::Usage budget_usage;
budget_usage.model = "acme-custom-v1";
budget_usage.input_tokens = 500;
budget_usage.output_tokens = 120;
if (!client.llmbudget().consume("chatbot-daily", 0, 0.0, budget_usage)) {
    std::cout << "daily LLM budget exceeded\n";
}
```

## Floating point arguments

Every `double` the SDK sends is formatted the same way, independent of the process
locale (`std::to_chars`, never `printf`/`ostream`), so a program that has called
`setlocale(LC_ALL, "de_DE.UTF-8")` still sends `1.5`, not `1,5`.

* Scores, deltas and weights (`ZADD`, `ZINCRBY`, `ZUNIONSTORE ... WEIGHTS`): the
  shortest text that reads back as the identical `double` (`0.1`, `100`, `1e-05`).
  `+inf`/`-inf` are sent as `inf`/`-inf`.
* Values the server only accepts when finite (vector components, `SCACHE`
  thresholds, GEO coordinates and radii, `INCRBYFLOAT`/`HINCRBYFLOAT`, rate-limit
  rates): NaN and infinity throw `hotkv::InvalidArgumentError` before anything is
  sent.
* USD amounts (`LLMBUDGET.SET/CONSUME`, `LLMSTATS.RECORD/PRICE`): fixed point
  rounded to the nearest micro-dollar, at most six decimals, trailing zeros
  trimmed, never an exponent (`0.00001`, not `1e-05`), because the server parses
  nothing else. A negative, non-finite or oversized (over about 1.8e13) amount
  throws `hotkv::InvalidArgumentError`.
* `Client::execute()` formats floating point arguments the same way. `PCACHE`
  cost-per-hit values must be finite and non-negative.

Doubles in replies are parsed locale independently too.

## Error handling

Every exception the SDK throws derives from `hotkv::Error : std::runtime_error`:

```
hotkv::Error
  - hotkv::ConnectionError
    - hotkv::TimeoutError
  - hotkv::ProtocolError
  - hotkv::ReplyTypeError
  - hotkv::InvalidArgumentError   (argument rejected locally: NaN, negative USD amount, ...)
  - hotkv::ServerError            (code() names the RESP error prefix, e.g. "WRONGTYPE")
    - hotkv::NotLicensedError     (code() == "NOLICENSE")
    - hotkv::ClusterRedirectError
      - hotkv::MovedError         (slot(), host(), port())
      - hotkv::AskError           (slot(), host(), port())
```

```cpp
try {
    client.set("k", "v");
} catch (const hotkv::NotLicensedError&) {
    // enterprise feature not licensed on this server
} catch (const hotkv::TimeoutError&) {
    // connect/command timeout
} catch (const hotkv::ServerError& e) {
    std::cerr << e.code() << ": " << e.what() << "\n";
} catch (const hotkv::Error& e) {
    std::cerr << "hotkv error: " << e.what() << "\n";
}
```

A nil bulk reply is never an exception: it maps to an empty `std::optional`
(`client.get("missing")` returns `std::nullopt`, not a thrown error).

## Testing

Unit tests need no server:

```bash
cmake -S . -B build -DHOTKV_CPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build -R hotkv_cpp_unit_tests
```

Integration tests run against a live HotKV server, and are skipped automatically
(with a clear reason) when `HOTKV_TEST_URL` is not set:

```bash
export HOTKV_TEST_URL=hotkv://127.0.0.1:6379
ctest --test-dir build -R hotkv_cpp_integration_tests
```

The integration tests create keys under unique prefixes and clean up, but use
databases 12 to 15 for a few cases, so point them at a throw-away server. Enterprise
and AI commands need a licensed server; with an unlicensed one those tests skip with
the feature name, so check the skipped count. `HOTKV_TEST_ALLOW_FLUSHALL=1` also
enables the one test that runs `FLUSHALL`.

Further opt-in suites are skipped unless their environment variables are set
(see the header of the test file for each): TLS, mutual TLS and password tests
(`HOTKV_TEST_TLS_HOST`, `HOTKV_TEST_TLS_CA`, `HOTKV_TEST_TLS_MTLS_PORT`,
`HOTKV_TEST_TLS_CERT`, `HOTKV_TEST_TLS_KEY`, `HOTKV_TEST_TLS_AUTH_PORT`,
`HOTKV_TEST_AUTH_PASSWORD`; `tests/integration/test_tls_auth.cpp`) and a session
against a remote TLS + password instance (`HOTKV_REMOTE_HOST`, `HOTKV_REMOTE_PORT`,
`HOTKV_REMOTE_CA`, `HOTKV_REMOTE_PASSWORD`, read from the environment only;
`tests/integration/test_remote.cpp`).

## Contributing

Bug reports and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Support

- Bugs and feature requests: [GitHub issues](https://github.com/hotkv-ltd/hotkv-cpp-client/issues)
- Questions and commercial support: support@hotkv.com
- Security vulnerabilities: see [SECURITY.md](SECURITY.md)

## License

Licensed under the Apache License, Version 2.0: see [LICENSE](LICENSE) and [NOTICE](NOTICE).

## Trademarks and affiliation

"HotKV" is a trademark of HotKV Ltd; the license does not grant rights to use it.
The HotKV server is a separate commercial product, and its enterprise commands need a HotKV license.

HotKV is an independent product of HotKV Ltd. It speaks the RESP protocol and implements
many Redis commands so that existing tools and client habits carry over, but it is not Redis,
Valkey, Dragonfly or KeyDB, and this SDK is built for and tested against HotKV. HotKV Ltd is not
affiliated with, endorsed by or sponsored by Redis Ltd., the Valkey project, DragonflyDB or KeyDB.
Redis is a registered trademark of Redis Ltd. Valkey, Dragonfly, KeyDB and all other product and
company names are trademarks of their respective owners; they are used here only to describe
protocol and command compatibility.
