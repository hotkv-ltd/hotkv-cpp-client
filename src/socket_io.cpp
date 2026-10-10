// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include "socket_io.hpp"

#include <hotkv/error.hpp>

#include <cstring>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
namespace {
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
}
#else
#include <arpa/inet.h>
#include <csignal>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
using socket_t = int;
namespace {
constexpr socket_t kInvalidSocket = -1;
}
#endif

#ifdef HOTKV_CPP_WITH_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace hotkv::detail {

namespace {

#ifdef _WIN32

void ensure_platform_ready() {
    static bool done = [] {
        WSADATA data;
        ::WSAStartup(MAKEWORD(2, 2), &data);
        return true;
    }();
    (void)done;
}

std::string last_socket_error_message() {
    return "winsock error " + std::to_string(::WSAGetLastError());
}

void close_socket(socket_t fd) { ::closesocket(fd); }

void set_nonblocking(socket_t fd, bool nonblocking) {
    u_long mode = nonblocking ? 1 : 0;
    ::ioctlsocket(fd, FIONBIO, &mode);
}

#else

void ensure_platform_ready() {
    static bool done = [] {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)done;
}

std::string last_socket_error_message() { return std::strerror(errno); }

void close_socket(socket_t fd) { ::close(fd); }

void set_nonblocking(socket_t fd, bool nonblocking) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return;
    }
    if (nonblocking) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    } else {
        ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
}

#endif

void apply_socket_timeouts(socket_t fd, std::chrono::milliseconds recv, std::chrono::milliseconds send) {
#ifdef _WIN32
    DWORD rtimeo = static_cast<DWORD>(recv.count());
    DWORD stimeo = static_cast<DWORD>(send.count());
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rtimeo), sizeof(rtimeo));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&stimeo), sizeof(stimeo));
#else
    timeval rtv{};
    rtv.tv_sec = static_cast<time_t>(recv.count() / 1000);
    rtv.tv_usec = static_cast<suseconds_t>((recv.count() % 1000) * 1000);
    timeval stv{};
    stv.tv_sec = static_cast<time_t>(send.count() / 1000);
    stv.tv_usec = static_cast<suseconds_t>((send.count() % 1000) * 1000);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
#endif
}

socket_t connect_tcp(const std::string& host, uint16_t port, std::chrono::milliseconds timeout) {
    ensure_platform_ready();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    std::string port_str = std::to_string(port);
    int rc = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &resolved);
    if (rc != 0 || resolved == nullptr) {
        throw ConnectionError("hotkv: cannot resolve host \"" + host + "\"");
    }

    struct AddrGuard {
        addrinfo* p;
        ~AddrGuard() { ::freeaddrinfo(p); }
    } guard{resolved};

    std::string last_error = "no address resolved";
    for (addrinfo* ai = resolved; ai != nullptr; ai = ai->ai_next) {
        socket_t fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == kInvalidSocket) {
            last_error = last_socket_error_message();
            continue;
        }
        set_nonblocking(fd, true);

#ifdef _WIN32
        int rc2 = ::connect(fd, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
        bool in_progress = rc2 == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
        int rc2 = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        bool in_progress = rc2 == -1 && errno == EINPROGRESS;
#endif
        if (rc2 == 0) {
            set_nonblocking(fd, false);
            return fd;
        }
        if (!in_progress) {
            last_error = last_socket_error_message();
            close_socket(fd);
            continue;
        }

#ifdef _WIN32
        fd_set write_set;
        fd_set err_set;
        FD_ZERO(&write_set);
        FD_SET(fd, &write_set);
        FD_ZERO(&err_set);
        FD_SET(fd, &err_set);
        timeval tv{};
        tv.tv_sec = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
        int sel = ::select(0, nullptr, &write_set, &err_set, &tv);
        bool writable = sel > 0 && FD_ISSET(fd, &write_set);
#else
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int sel = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        bool writable = sel > 0 && (pfd.revents & POLLOUT) != 0 && (pfd.revents & (POLLERR | POLLHUP)) == 0;
#endif
        if (sel == 0) {
            close_socket(fd);
            throw TimeoutError("hotkv: connect to " + host + ":" + port_str + " timed out");
        }
        if (!writable) {
            last_error = last_socket_error_message();
            close_socket(fd);
            continue;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
        if (so_error != 0) {
            last_error = std::strerror(so_error);
            close_socket(fd);
            continue;
        }
        set_nonblocking(fd, false);
        return fd;
    }
    throw ConnectionError("hotkv: could not connect to " + host + ":" + port_str + ": " + last_error);
}

#ifndef _WIN32
socket_t connect_unix(const std::string& path) {
    socket_t fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == kInvalidSocket) {
        throw ConnectionError("hotkv: cannot create unix socket: " + last_socket_error_message());
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        close_socket(fd);
        throw ConnectionError("hotkv: unix socket path too long: \"" + path + "\"");
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::string err = last_socket_error_message();
        close_socket(fd);
        throw ConnectionError("hotkv: cannot connect to unix socket \"" + path + "\": " + err);
    }
    return fd;
}
#endif

// A plain (non-TLS) TCP or unix domain socket stream.
class PlainSocketStream : public IoStream {
public:
    explicit PlainSocketStream(socket_t fd) : fd_(fd) {}
    ~PlainSocketStream() override { close(); }

    std::size_t read_some(char* buf, std::size_t len) override {
        for (;;) {
#ifdef _WIN32
            int n = ::recv(fd_, buf, static_cast<int>(len), 0);
            if (n >= 0) {
                return static_cast<std::size_t>(n);
            }
            int err = ::WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
            if (err == WSAETIMEDOUT) {
                throw TimeoutError("hotkv: read timed out");
            }
            throw ConnectionError("hotkv: read failed: winsock error " + std::to_string(err));
#else
            ssize_t n = ::recv(fd_, buf, len, 0);
            if (n >= 0) {
                return static_cast<std::size_t>(n);
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                throw TimeoutError("hotkv: read timed out");
            }
            throw ConnectionError(std::string("hotkv: read failed: ") + std::strerror(errno));
#endif
        }
    }

    void write_all(const char* buf, std::size_t len) override {
        std::size_t sent = 0;
        while (sent < len) {
#ifdef _WIN32
            int n = ::send(fd_, buf + sent, static_cast<int>(len - sent), 0);
            if (n < 0) {
                int err = ::WSAGetLastError();
                if (err == WSAEINTR) {
                    continue;
                }
                if (err == WSAETIMEDOUT) {
                    throw TimeoutError("hotkv: write timed out");
                }
                throw ConnectionError("hotkv: write failed: winsock error " + std::to_string(err));
            }
#else
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags |= MSG_NOSIGNAL;
#endif
            ssize_t n = ::send(fd_, buf + sent, len - sent, flags);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    throw TimeoutError("hotkv: write timed out");
                }
                throw ConnectionError(std::string("hotkv: write failed: ") + std::strerror(errno));
            }
#endif
            sent += static_cast<std::size_t>(n);
        }
    }

    void close() noexcept override {
        if (fd_ != kInvalidSocket) {
            close_socket(fd_);
            fd_ = kInvalidSocket;
        }
    }

    void set_timeouts(std::chrono::milliseconds recv, std::chrono::milliseconds send) override {
        apply_socket_timeouts(fd_, recv, send);
    }

private:
    socket_t fd_;
};

#ifdef HOTKV_CPP_WITH_TLS

std::string openssl_error_string() {
    unsigned long code = ERR_get_error();
    if (code == 0) {
        return "unknown TLS error";
    }
    char buf[256];
    ERR_error_string_n(code, buf, sizeof(buf));
    return std::string(buf);
}

class TlsSocketStream : public IoStream {
public:
    TlsSocketStream(socket_t fd, SSL_CTX* ctx, SSL* ssl) : fd_(fd), ctx_(ctx), ssl_(ssl) {}
    ~TlsSocketStream() override { close(); }

    std::size_t read_some(char* buf, std::size_t len) override {
        int n = SSL_read(ssl_, buf, static_cast<int>(len));
        if (n > 0) {
            return static_cast<std::size_t>(n);
        }
        return handle_io_result(n, "read");
    }

    void write_all(const char* buf, std::size_t len) override {
        std::size_t sent = 0;
        while (sent < len) {
            int n = SSL_write(ssl_, buf + sent, static_cast<int>(len - sent));
            if (n > 0) {
                sent += static_cast<std::size_t>(n);
                continue;
            }
            handle_io_result(n, "write");
        }
    }

    void close() noexcept override {
        if (ssl_ != nullptr) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (ctx_ != nullptr) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
        }
        if (fd_ != kInvalidSocket) {
            close_socket(fd_);
            fd_ = kInvalidSocket;
        }
    }

    void set_timeouts(std::chrono::milliseconds recv, std::chrono::milliseconds send) override {
        apply_socket_timeouts(fd_, recv, send);
    }

private:
    // Returns 0 (clean EOF) for a graceful TLS close; throws for
    // anything else. WANT_READ/WANT_WRITE cannot happen on a blocking
    // socket with a plain read/write call, but are handled defensively.
    std::size_t handle_io_result(int rc, const char* what) {
        int err = SSL_get_error(ssl_, rc);
        switch (err) {
            case SSL_ERROR_ZERO_RETURN:
                return 0;
            case SSL_ERROR_SYSCALL: {
#ifndef _WIN32
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    throw TimeoutError(std::string("hotkv: TLS ") + what + " timed out");
                }
#endif
                return 0;
            }
            default:
                throw ConnectionError(std::string("hotkv: TLS ") + what + " failed: " + openssl_error_string());
        }
    }

    socket_t fd_;
    SSL_CTX* ctx_;
    SSL* ssl_;
};

bool is_ip_literal(const std::string& text) {
    unsigned char buf[sizeof(struct in6_addr)];
    return ::inet_pton(AF_INET, text.c_str(), buf) == 1 || ::inet_pton(AF_INET6, text.c_str(), buf) == 1;
}

std::unique_ptr<IoStream> connect_tls(socket_t fd, const Options& opts) {
    static std::once_flag init_flag;
    std::call_once(init_flag, [] { OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr); });

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == nullptr) {
        close_socket(fd);
        throw ConnectionError("hotkv: failed to create TLS context: " + openssl_error_string());
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    if (opts.tls.insecure_skip_verify) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        if (!opts.tls.ca_file.empty()) {
            // An explicit CA bundle that cannot be read is an error, never a
            // silent fall back to the system trust store.
            if (SSL_CTX_load_verify_locations(ctx, opts.tls.ca_file.c_str(), nullptr) != 1) {
                std::string err = openssl_error_string();
                SSL_CTX_free(ctx);
                close_socket(fd);
                throw ConnectionError("hotkv: failed to load TLS CA file \"" + opts.tls.ca_file + "\": " + err);
            }
        } else {
            SSL_CTX_set_default_verify_paths(ctx);
        }
    }
    if (!opts.tls.cert_file.empty() &&
        SSL_CTX_use_certificate_file(ctx, opts.tls.cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        std::string err = openssl_error_string();
        SSL_CTX_free(ctx);
        close_socket(fd);
        throw ConnectionError("hotkv: failed to load TLS client certificate: " + err);
    }
    if (!opts.tls.key_file.empty() && SSL_CTX_use_PrivateKey_file(ctx, opts.tls.key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        std::string err = openssl_error_string();
        SSL_CTX_free(ctx);
        close_socket(fd);
        throw ConnectionError("hotkv: failed to load TLS client private key: " + err);
    }

    SSL* ssl = SSL_new(ctx);
    if (ssl == nullptr) {
        SSL_CTX_free(ctx);
        close_socket(fd);
        throw ConnectionError("hotkv: failed to create TLS session: " + openssl_error_string());
    }

    std::string server_name = opts.tls.server_name.empty() ? opts.host : opts.tls.server_name;
    if (!server_name.empty()) {
        // An IP address is matched against the certificate's IP subject
        // alternative names (a DNS name check never matches one) and is not
        // sent as SNI, which only carries host names (RFC 6066).
        const bool is_ip = is_ip_literal(server_name);
        if (!is_ip) {
            SSL_set_tlsext_host_name(ssl, server_name.c_str());
        }
        if (!opts.tls.insecure_skip_verify) {
            X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
            if (is_ip) {
                X509_VERIFY_PARAM_set1_ip_asc(param, server_name.c_str());
            } else {
                X509_VERIFY_PARAM_set1_host(param, server_name.c_str(), server_name.size());
            }
        }
    }
    SSL_set_fd(ssl, static_cast<int>(fd));
    apply_socket_timeouts(fd, opts.connect_timeout, opts.connect_timeout);

    if (SSL_connect(ssl) != 1) {
        std::string err = openssl_error_string();
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close_socket(fd);
        throw ConnectionError("hotkv: TLS handshake failed: " + err);
    }

    return std::make_unique<TlsSocketStream>(fd, ctx, ssl);
}

#endif // HOTKV_CPP_WITH_TLS

} // namespace

std::unique_ptr<IoStream> open_connection(const Options& opts) {
    socket_t fd;
    if (!opts.unix_socket_path.empty()) {
#ifdef _WIN32
        throw ConnectionError("hotkv: unix sockets are not supported on this platform");
#else
        ensure_platform_ready();
        fd = connect_unix(opts.unix_socket_path);
#endif
    } else {
        fd = connect_tcp(opts.host, opts.port, opts.connect_timeout);
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    }

    if (opts.tls.enabled) {
#ifdef HOTKV_CPP_WITH_TLS
        return connect_tls(fd, opts);
#else
        close_socket(fd);
        throw ConnectionError(
            "hotkv: TLS was requested but this build of hotkv-cpp was compiled without TLS support "
            "(HOTKV_CPP_WITH_TLS)");
#endif
    }

    auto stream = std::make_unique<PlainSocketStream>(fd);
    stream->set_timeouts(opts.command_timeout, opts.command_timeout);
    return stream;
}

} // namespace hotkv::detail
