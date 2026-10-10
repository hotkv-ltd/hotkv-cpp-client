// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/pipeline.hpp>

#include "client_impl.hpp"

namespace hotkv {

Pipeline::Pipeline(std::shared_ptr<detail::ClientImpl> impl) : impl_(std::move(impl)) {}

Pipeline& Pipeline::command(std::vector<std::string> args) {
    commands_.push_back(std::move(args));
    return *this;
}

std::vector<Reply> Pipeline::exec() {
    if (commands_.empty()) {
        return {};
    }

    detail::ConnectionPool::Lease lease = impl_->pool().acquire();
    try {
        for (const auto& cmd : commands_) {
            lease->write_command(cmd);
        }
        std::vector<Reply> replies;
        replies.reserve(commands_.size());
        for (std::size_t i = 0; i < commands_.size(); ++i) {
            replies.push_back(lease->read_one_reply());
        }
        lease->touch();
        commands_.clear();
        return replies;
    } catch (...) {
        lease.mark_broken();
        throw;
    }
}

} // namespace hotkv
