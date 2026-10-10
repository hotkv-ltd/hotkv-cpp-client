// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/transaction.hpp>

#include "client_impl.hpp"

#include <hotkv/error.hpp>

namespace hotkv {

namespace detail {

class TransactionImpl {
public:
    explicit TransactionImpl(std::shared_ptr<ClientImpl> client_impl)
        : client_impl_(std::move(client_impl)), lease_(client_impl_->pool().acquire()) {}

    std::shared_ptr<ClientImpl> client_impl_;
    ConnectionPool::Lease lease_;
    bool in_multi_ = false;
    bool finished_ = false;
};

} // namespace detail

Transaction::Transaction(std::shared_ptr<detail::ClientImpl> impl)
    : impl_(std::make_unique<detail::TransactionImpl>(std::move(impl))) {}

Transaction::Transaction(Transaction&&) noexcept = default;
Transaction& Transaction::operator=(Transaction&&) noexcept = default;

Transaction::~Transaction() {
    if (impl_ && impl_->in_multi_ && !impl_->finished_) {
        try {
            impl_->lease_->write_command({"DISCARD"});
            impl_->lease_->read_one_reply();
        } catch (...) {
            impl_->lease_.mark_broken();
        }
    }
}

Transaction& Transaction::watch(const std::vector<std::string>& keys) {
    std::vector<std::string> args{"WATCH"};
    args.insert(args.end(), keys.begin(), keys.end());
    try {
        impl_->lease_->write_command(args);
        impl_->lease_->read_one_reply().raise_if_error();
    } catch (const ConnectionError&) {
        impl_->lease_.mark_broken();
        throw;
    }
    return *this;
}

void Transaction::unwatch() {
    try {
        impl_->lease_->write_command({"UNWATCH"});
        impl_->lease_->read_one_reply().raise_if_error();
    } catch (const ConnectionError&) {
        impl_->lease_.mark_broken();
        throw;
    }
}

Transaction& Transaction::multi() {
    try {
        impl_->lease_->write_command({"MULTI"});
        impl_->lease_->read_one_reply().raise_if_error();
        impl_->in_multi_ = true;
    } catch (const ConnectionError&) {
        impl_->lease_.mark_broken();
        throw;
    }
    return *this;
}

Transaction& Transaction::command(std::vector<std::string> args) {
    try {
        impl_->lease_->write_command(args);
        Reply reply = impl_->lease_->read_one_reply();
        reply.raise_if_error(); // e.g. wrong arity while queuing; transaction stays open (EXEC will abort it)
        if (reply.type() != ReplyType::SimpleString || reply.as_string() != "QUEUED") {
            throw ProtocolError("hotkv: expected +QUEUED while queuing a transaction command");
        }
    } catch (const ConnectionError&) {
        impl_->lease_.mark_broken();
        throw;
    }
    return *this;
}

std::optional<std::vector<Reply>> Transaction::exec() {
    try {
        impl_->lease_->write_command({"EXEC"});
        Reply reply = impl_->lease_->read_one_reply();
        impl_->finished_ = true;
        if (reply.is_nil()) {
            return std::nullopt;
        }
        reply.raise_if_error();
        const Reply::Array& arr = reply.as_array();
        return std::vector<Reply>(arr.begin(), arr.end());
    } catch (const ConnectionError&) {
        impl_->lease_.mark_broken();
        impl_->finished_ = true;
        throw;
    }
}

void Transaction::discard() {
    try {
        impl_->lease_->write_command({"DISCARD"});
        impl_->lease_->read_one_reply().raise_if_error();
        impl_->finished_ = true;
    } catch (const ConnectionError&) {
        impl_->lease_.mark_broken();
        impl_->finished_ = true;
        throw;
    }
}

} // namespace hotkv
