// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "client_impl.hpp"

#include <hotkv/client.hpp>
#include <hotkv/pipeline.hpp>
#include <hotkv/subscriber.hpp>
#include <hotkv/transaction.hpp>

namespace hotkv {

Client::Client(Options opts) : impl_(std::make_shared<detail::ClientImpl>(std::move(opts))) {}

Client::Client(std::shared_ptr<detail::ClientImpl> impl) : impl_(std::move(impl)) {}

Client Client::connect(const std::string& url) {
    return Client(Options::parse_url(url));
}

Client Client::connect(const Options& options) {
    return Client(options);
}

const Options& Client::options() const noexcept {
    return impl_->options();
}

Reply Client::command(std::vector<std::string> args) {
    return impl_->command(args);
}

Pipeline Client::pipeline() {
    return Pipeline(impl_);
}

Transaction Client::transaction() {
    return Transaction(impl_);
}

Subscriber Client::create_subscriber() {
    return Subscriber(impl_->options());
}

} // namespace hotkv
