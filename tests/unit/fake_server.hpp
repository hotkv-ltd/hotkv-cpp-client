// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#pragma once

// A tiny in-process RESP2 server for unit tests: listens on 127.0.0.1 on an
// ephemeral port, decodes each command (an array of bulk strings), records
// it, and answers with whatever the test's handler returns (raw RESP
// bytes). It lets the real Client / ClusterClient code run end to end
// (sockets, pool, pipeline) against scripted replies. POSIX only.

#ifndef _WIN32

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hotkv::testutil {

struct RecordedCommand {
    int connection = 0;
    std::vector<std::string> argv;
};

class FakeServer {
public:
    // handler(connection_id, argv) -> raw RESP reply bytes ("" sends nothing).
    using Handler = std::function<std::string(int, const std::vector<std::string>&)>;

    explicit FakeServer(Handler handler) : handler_(std::move(handler)) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        ::listen(listen_fd_, 16);
        acceptor_ = std::thread([this] { accept_loop(); });
    }

    ~FakeServer() {
        stopping_ = true;
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        if (acceptor_.joinable()) {
            acceptor_.join();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (int fd : client_fds_) {
                ::shutdown(fd, SHUT_RDWR);
            }
        }
        for (auto& t : workers_) {
            if (t.joinable()) {
                t.join();
            }
        }
        for (int fd : client_fds_) {
            ::close(fd);
        }
    }

    FakeServer(const FakeServer&) = delete;
    FakeServer& operator=(const FakeServer&) = delete;

    uint16_t port() const { return port_; }

    // Snapshot of every command received so far, in arrival order.
    std::vector<RecordedCommand> commands() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return log_;
    }

    // Commands whose first word equals `name` (case-sensitive upper case).
    std::vector<RecordedCommand> commands_named(const std::string& name) const {
        std::vector<RecordedCommand> out;
        for (auto& c : commands()) {
            if (!c.argv.empty() && c.argv[0] == name) {
                out.push_back(c);
            }
        }
        return out;
    }

    // The standard answer to the connection handshake: HELLO is refused
    // (the client falls back to RESP2) and everything else is OK.
    static std::string handshake_reply(const std::vector<std::string>& argv) {
        if (!argv.empty() && argv[0] == "HELLO") {
            return "-ERR unknown command 'HELLO'\r\n";
        }
        return "+OK\r\n";
    }

private:
    void accept_loop() {
        int next_conn = 0;
        while (!stopping_) {
            int fd = ::accept(listen_fd_, nullptr, nullptr);
            if (fd < 0) {
                return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            client_fds_.push_back(fd);
            int conn = next_conn++;
            workers_.emplace_back([this, fd, conn] { serve(fd, conn); });
        }
    }

    static bool read_line(int fd, std::string& buf, std::string& line) {
        for (;;) {
            auto pos = buf.find("\r\n");
            if (pos != std::string::npos) {
                line = buf.substr(0, pos);
                buf.erase(0, pos + 2);
                return true;
            }
            char tmp[4096];
            ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
            if (n <= 0) {
                return false;
            }
            buf.append(tmp, static_cast<std::size_t>(n));
        }
    }

    static bool read_exact(int fd, std::string& buf, std::size_t n, std::string& out) {
        while (buf.size() < n + 2) {
            char tmp[4096];
            ssize_t got = ::recv(fd, tmp, sizeof(tmp), 0);
            if (got <= 0) {
                return false;
            }
            buf.append(tmp, static_cast<std::size_t>(got));
        }
        out = buf.substr(0, n);
        buf.erase(0, n + 2);
        return true;
    }

    void serve(int fd, int conn) {
        std::string buf;
        std::string line;
        while (read_line(fd, buf, line)) {
            if (line.empty() || line[0] != '*') {
                break;
            }
            int argc = std::atoi(line.c_str() + 1);
            std::vector<std::string> argv;
            bool ok = true;
            for (int i = 0; i < argc; ++i) {
                if (!read_line(fd, buf, line) || line.empty() || line[0] != '$') {
                    ok = false;
                    break;
                }
                std::string arg;
                if (!read_exact(fd, buf, static_cast<std::size_t>(std::atoi(line.c_str() + 1)), arg)) {
                    ok = false;
                    break;
                }
                argv.push_back(std::move(arg));
            }
            if (!ok) {
                break;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                log_.push_back(RecordedCommand{conn, argv});
            }
            std::string reply = handler_(conn, argv);
            std::size_t off = 0;
            while (off < reply.size()) {
                ssize_t n = ::send(fd, reply.data() + off, reply.size() - off, MSG_NOSIGNAL);
                if (n <= 0) {
                    return;
                }
                off += static_cast<std::size_t>(n);
            }
        }
    }

    Handler handler_;
    int listen_fd_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::thread acceptor_;
    mutable std::mutex mutex_;
    std::vector<std::thread> workers_;
    std::vector<int> client_fds_;
    std::vector<RecordedCommand> log_;
};

} // namespace hotkv::testutil

#endif // !_WIN32
