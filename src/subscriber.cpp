// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/subscriber.hpp>

#include "connection.hpp"

#include <hotkv/error.hpp>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace hotkv {

namespace detail {

class SubscriberImpl {
public:
    explicit SubscriberImpl(Options opts) : options_(std::move(opts)), conn_(std::make_unique<Connection>(options_)) {}

    ~SubscriberImpl() {
        stop_requested_.store(true);
        if (listener_thread_.joinable()) {
            listener_thread_.join();
        }
    }

    Options options_;
    std::unique_ptr<Connection> conn_;
    std::mutex mutex_;

    std::vector<std::string> channels_;
    std::vector<std::string> patterns_;
    std::vector<std::string> shard_channels_;

    // Messages that arrived while a (un)subscribe call was still waiting
    // for its confirmation frames; delivered before anything new is read.
    std::deque<Message> pending_;

    std::thread listener_thread_;
    std::atomic<bool> stop_requested_{false};
    std::function<void(const Message&)> handler_;
};

} // namespace detail

namespace {

std::optional<Message> to_message(const Reply& reply) {
    if (!reply.is_array_like()) {
        return std::nullopt;
    }
    const Reply::Array& arr = reply.as_array();
    if (arr.empty() || arr[0].is_nil()) {
        return std::nullopt;
    }
    std::string kind = arr[0].as_string();
    if (kind == "message" && arr.size() >= 3) {
        Message m;
        m.kind = kind;
        m.channel = arr[1].is_nil() ? std::string() : arr[1].as_string();
        m.payload = arr[2].is_nil() ? std::string() : arr[2].as_string();
        return m;
    }
    if (kind == "pmessage" && arr.size() >= 4) {
        Message m;
        m.kind = kind;
        m.pattern = arr[1].is_nil() ? std::string() : arr[1].as_string();
        m.channel = arr[2].is_nil() ? std::string() : arr[2].as_string();
        m.payload = arr[3].is_nil() ? std::string() : arr[3].as_string();
        return m;
    }
    if (kind == "smessage" && arr.size() >= 3) {
        Message m;
        m.kind = kind;
        m.channel = arr[1].is_nil() ? std::string() : arr[1].as_string();
        m.payload = arr[2].is_nil() ? std::string() : arr[2].as_string();
        return m;
    }
    // subscribe/psubscribe/ssubscribe/unsubscribe/punsubscribe/sunsubscribe confirmation: not a message.
    return std::nullopt;
}

void remove_all(std::vector<std::string>& tracked, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        tracked.erase(std::remove(tracked.begin(), tracked.end(), name), tracked.end());
    }
}

} // namespace

Subscriber::Subscriber(Options options) : impl_(std::make_unique<detail::SubscriberImpl>(std::move(options))) {}

Subscriber::Subscriber(Subscriber&&) noexcept = default;
Subscriber& Subscriber::operator=(Subscriber&&) noexcept = default;

Subscriber::~Subscriber() {
    if (impl_) {
        stop();
    }
}

namespace {

void send_and_drain(detail::SubscriberImpl& impl, const std::string& command_name,
                     const std::vector<std::string>& targets, std::size_t confirmations_expected) {
    std::vector<std::string> args{command_name};
    args.insert(args.end(), targets.begin(), targets.end());
    impl.conn_->set_io_timeouts(impl.options_.command_timeout, impl.options_.command_timeout);
    impl.conn_->write_command(args);
    std::size_t confirmed = 0;
    while (confirmed < confirmations_expected) {
        Reply reply = impl.conn_->read_one_reply();
        if (auto msg = to_message(reply)) {
            // A published message overtook the confirmation: keep it.
            impl.pending_.push_back(std::move(*msg));
            continue;
        }
        ++confirmed;
    }
}

} // namespace

void Subscriber::subscribe(const std::vector<std::string>& channels) {
    if (channels.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    send_and_drain(*impl_, "SUBSCRIBE", channels, channels.size());
    impl_->channels_.insert(impl_->channels_.end(), channels.begin(), channels.end());
}

void Subscriber::psubscribe(const std::vector<std::string>& patterns) {
    if (patterns.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    send_and_drain(*impl_, "PSUBSCRIBE", patterns, patterns.size());
    impl_->patterns_.insert(impl_->patterns_.end(), patterns.begin(), patterns.end());
}

void Subscriber::ssubscribe(const std::vector<std::string>& channels) {
    if (channels.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    send_and_drain(*impl_, "SSUBSCRIBE", channels, channels.size());
    impl_->shard_channels_.insert(impl_->shard_channels_.end(), channels.begin(), channels.end());
}

void Subscriber::unsubscribe(const std::vector<std::string>& channels) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::size_t expected = channels.empty() ? impl_->channels_.size() : channels.size();
    send_and_drain(*impl_, "UNSUBSCRIBE", channels, std::max<std::size_t>(1, expected));
    if (channels.empty()) {
        impl_->channels_.clear();
    } else {
        remove_all(impl_->channels_, channels);
    }
}

void Subscriber::punsubscribe(const std::vector<std::string>& patterns) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::size_t expected = patterns.empty() ? impl_->patterns_.size() : patterns.size();
    send_and_drain(*impl_, "PUNSUBSCRIBE", patterns, std::max<std::size_t>(1, expected));
    if (patterns.empty()) {
        impl_->patterns_.clear();
    } else {
        remove_all(impl_->patterns_, patterns);
    }
}

void Subscriber::sunsubscribe(const std::vector<std::string>& channels) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::size_t expected = channels.empty() ? impl_->shard_channels_.size() : channels.size();
    send_and_drain(*impl_, "SUNSUBSCRIBE", channels, std::max<std::size_t>(1, expected));
    if (channels.empty()) {
        impl_->shard_channels_.clear();
    } else {
        remove_all(impl_->shard_channels_, channels);
    }
}

std::optional<Message> Subscriber::next_message(std::optional<std::chrono::milliseconds> timeout) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (!impl_->pending_.empty()) {
        Message m = std::move(impl_->pending_.front());
        impl_->pending_.pop_front();
        return m;
    }
    std::optional<std::chrono::steady_clock::time_point> deadline;
    if (timeout.has_value()) {
        deadline = std::chrono::steady_clock::now() + *timeout;
    }
    for (;;) {
        std::chrono::milliseconds wait_for{0}; // 0 = block indefinitely
        if (deadline.has_value()) {
            auto remaining = *deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                return std::nullopt;
            }
            wait_for = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
                                 std::chrono::milliseconds(1));
        }
        impl_->conn_->set_io_timeouts(wait_for, wait_for);
        Reply reply;
        try {
            reply = impl_->conn_->read_one_reply();
        } catch (const TimeoutError&) {
            return std::nullopt;
        }
        if (auto msg = to_message(reply)) {
            return msg;
        }
    }
}

void Subscriber::listen(std::function<void(const Message&)> handler) {
    stop();
    impl_->handler_ = std::move(handler);
    impl_->stop_requested_.store(false);
    impl_->listener_thread_ = std::thread([this] {
        const auto poll_interval = std::chrono::milliseconds(500);
        while (!impl_->stop_requested_.load()) {
            std::optional<Message> msg;
            {
                std::lock_guard<std::mutex> lock(impl_->mutex_);
                if (!impl_->pending_.empty()) {
                    msg = std::move(impl_->pending_.front());
                    impl_->pending_.pop_front();
                }
                impl_->conn_->set_io_timeouts(poll_interval, poll_interval);
                try {
                    if (!msg) {
                        Reply reply = impl_->conn_->read_one_reply();
                        msg = to_message(reply);
                    }
                } catch (const TimeoutError&) {
                    continue;
                } catch (...) {
                    break;
                }
            }
            if (msg) {
                try {
                    impl_->handler_(*msg);
                } catch (...) {
                    // handler_ should not throw; ignored so the listener keeps running.
                }
            }
        }
    });
}

void Subscriber::stop() {
    if (!impl_) {
        return;
    }
    impl_->stop_requested_.store(true);
    if (impl_->listener_thread_.joinable()) {
        impl_->listener_thread_.join();
    }
}

} // namespace hotkv
