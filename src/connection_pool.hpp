// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

#include "connection.hpp"

#include <hotkv/options.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

namespace hotkv::detail {

// A bounded pool of Connections to one HotKV endpoint. Thread-safe:
// acquire() may be called concurrently from any number of threads.
class ConnectionPool {
public:
    explicit ConnectionPool(Options opts) : opts_(std::move(opts)) {}

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // RAII handle for one borrowed Connection. Returned to the pool for
    // reuse on destruction unless mark_broken() was called, in which
    // case it is closed and the pool's slot count is freed for a new one.
    class Lease {
    public:
        Lease() = default;
        Lease(ConnectionPool* pool, std::unique_ptr<Connection> conn) : pool_(pool), conn_(std::move(conn)) {}
        Lease(const Lease&) = delete;
        Lease(Lease&& other) noexcept
            : pool_(other.pool_), conn_(std::move(other.conn_)), broken_(other.broken_) {
            other.pool_ = nullptr;
        }
        Lease& operator=(const Lease&) = delete;
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                finish();
                pool_ = other.pool_;
                conn_ = std::move(other.conn_);
                broken_ = other.broken_;
                other.pool_ = nullptr;
            }
            return *this;
        }
        ~Lease() { finish(); }

        Connection& operator*() const noexcept { return *conn_; }
        Connection* operator->() const noexcept { return conn_.get(); }
        explicit operator bool() const noexcept { return conn_ != nullptr; }

        void mark_broken() noexcept { broken_ = true; }

    private:
        void finish() {
            if (pool_ != nullptr && conn_) {
                pool_->release(std::move(conn_), !broken_);
            }
            pool_ = nullptr;
        }

        ConnectionPool* pool_ = nullptr;
        std::unique_ptr<Connection> conn_;
        bool broken_ = false;
    };

    // Borrows a healthy connection, creating one if the pool has not
    // reached opts.pool_size yet, or blocking (up to opts.connect_timeout)
    // for one to be released otherwise. Throws hotkv::TimeoutError if no
    // connection becomes available in time, or whatever Connection's
    // constructor throws if a new connection cannot be established.
    Lease acquire();

    const Options& options() const noexcept { return opts_; }

private:
    void release(std::unique_ptr<Connection> conn, bool healthy);

    Options opts_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::unique_ptr<Connection>> idle_;
    std::size_t total_created_ = 0;
};

} // namespace hotkv::detail
