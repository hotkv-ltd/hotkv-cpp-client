// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

// Umbrella header for the official HotKV C++ SDK. Include this one
// header to get everything: #include <hotkv/hotkv.hpp>
//
//   hotkv::Client client = hotkv::Client::connect("hotkv://127.0.0.1:6379");
//   client.set("greeting", "hello");
//   std::optional<std::string> value = client.get("greeting");
//
// See README.md for the full quick start (TLS, pooling, pipelines,
// transactions, pub/sub, cluster, HotKV commands, AI caching).

#include <hotkv/ai.hpp>
#include <hotkv/client.hpp>
#include <hotkv/cluster.hpp>
#include <hotkv/enterprise.hpp>
#include <hotkv/enterprise_types.hpp>
#include <hotkv/error.hpp>
#include <hotkv/options.hpp>
#include <hotkv/pipeline.hpp>
#include <hotkv/reply.hpp>
#include <hotkv/scan.hpp>
#include <hotkv/subscriber.hpp>
#include <hotkv/transaction.hpp>
#include <hotkv/types.hpp>
#include <hotkv/version.hpp>
