// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "connection_pool.hpp"

#include <hotkv/error.hpp>

namespace hotkv::detail {

ConnectionPool::Lease ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        while (!idle_.empty()) {
            std::unique_ptr<Connection> conn = std::move(idle_.back());
            idle_.pop_back();

            auto now = std::chrono::steady_clock::now();
            auto idle_for = now - conn->last_used();

            bool expired = opts_.pool_idle_timeout.count() > 0 && idle_for > opts_.pool_idle_timeout;
            if (expired) {
                conn->close();
                if (total_created_ > 0) {
                    --total_created_;
                }
                continue;
            }

            bool needs_check =
                opts_.pool_health_check_after.count() > 0 && idle_for > opts_.pool_health_check_after;
            if (needs_check) {
                lock.unlock();
                bool healthy = conn->ping();
                lock.lock();
                if (!healthy) {
                    conn->close();
                    if (total_created_ > 0) {
                        --total_created_;
                    }
                    continue;
                }
            }

            return Lease(this, std::move(conn));
        }

        if (total_created_ < opts_.pool_size) {
            ++total_created_;
            lock.unlock();
            try {
                auto conn = std::make_unique<Connection>(opts_);
                return Lease(this, std::move(conn));
            } catch (...) {
                lock.lock();
                if (total_created_ > 0) {
                    --total_created_;
                }
                throw;
            }
        }

        if (cv_.wait_for(lock, opts_.connect_timeout) == std::cv_status::timeout) {
            throw TimeoutError("hotkv: timed out waiting for a free pooled connection (pool_size exhausted)");
        }
    }
}

void ConnectionPool::release(std::unique_ptr<Connection> conn, bool healthy) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (healthy) {
            idle_.push_back(std::move(conn));
        } else {
            conn->close();
            if (total_created_ > 0) {
                --total_created_;
            }
        }
    }
    cv_.notify_one();
}

} // namespace hotkv::detail
