#include "socket_wrapper.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <format>
#include <algorithm>

namespace hfdown {

class Socket::Impl {
public:
    int fd = -1;
    int timeout_sec = 30;
    std::string read_buffer;
    
    ~Impl() { if (fd >= 0) ::close(fd); }
};

Socket::Socket() : pImpl_(std::make_unique<Impl>()) {}
Socket::~Socket() = default;
Socket::Socket(Socket&&) noexcept = default;
Socket& Socket::operator=(Socket&&) noexcept = default;

void Socket::set_timeout(int seconds) {
    pImpl_->timeout_sec = seconds;
}

bool Socket::is_open() const {
    return pImpl_->fd >= 0;
}

void Socket::close() {
    if (pImpl_->fd >= 0) {
        ::close(pImpl_->fd);
        pImpl_->fd = -1;
    }
}

int Socket::fd() const {
    return pImpl_->fd;
}

std::expected<void, SocketErrorInfo> Socket::connect(const std::string& host, uint16_t port) {
    addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        return std::unexpected(SocketErrorInfo{SocketError::DNSError, 
            std::format("Failed to resolve host: {}", host)});
    }
    
    pImpl_->fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (pImpl_->fd < 0) {
        freeaddrinfo(result);
        return std::unexpected(SocketErrorInfo{SocketError::ConnectionFailed, "Failed to create socket"});
    }
    
    timeval tv{pImpl_->timeout_sec, 0};
    setsockopt(pImpl_->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(pImpl_->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    int flag = 1;
    setsockopt(pImpl_->fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    if (::connect(pImpl_->fd, result->ai_addr, result->ai_addrlen) < 0) {
        auto err = std::format("Connection failed to {}:{}", host, port);
        freeaddrinfo(result);
        ::close(pImpl_->fd);
        pImpl_->fd = -1;
        return std::unexpected(SocketErrorInfo{SocketError::ConnectionFailed, err});
    }
    
    freeaddrinfo(result);
    return {};
}

std::expected<size_t, SocketErrorInfo> Socket::write(std::span<const char> data) {
    if (pImpl_->fd < 0) {
        return std::unexpected(SocketErrorInfo{SocketError::WriteError, "Socket not connected"});
    }
    
    ssize_t sent = ::send(pImpl_->fd, data.data(), data.size(), 0);
    if (sent < 0) {
        return std::unexpected(SocketErrorInfo{SocketError::WriteError, 
            std::format("Write failed: {}", strerror(errno))});
    }
    return static_cast<size_t>(sent);
}

std::expected<size_t, SocketErrorInfo> Socket::read(std::span<char> buffer) {
    if (pImpl_->fd < 0) {
        return std::unexpected(SocketErrorInfo{SocketError::ReadError, "Socket not connected"});
    }
    
    if (!pImpl_->read_buffer.empty()) {
        size_t to_copy = std::min(buffer.size(), pImpl_->read_buffer.size());
        std::copy_n(pImpl_->read_buffer.begin(), to_copy, buffer.data());
        pImpl_->read_buffer.erase(0, to_copy);
        return to_copy;
    }
    
    ssize_t received = ::recv(pImpl_->fd, buffer.data(), buffer.size(), 0);
    if (received < 0) {
        return std::unexpected(SocketErrorInfo{SocketError::ReadError, 
            std::format("Read failed: {}", strerror(errno))});
    }
    return static_cast<size_t>(received);
}

std::expected<std::string, SocketErrorInfo> Socket::read_until(const std::string& delim) {
    std::array<char, 4096> temp_buf{};
    
    while (true) {
        if (auto pos = pImpl_->read_buffer.find(delim); pos != std::string::npos) {
            auto result = pImpl_->read_buffer.substr(0, pos + delim.length());
            pImpl_->read_buffer.erase(0, pos + delim.length());
            return result;
        }
        
        auto read_result = read(std::span{temp_buf});
        if (!read_result) return std::unexpected(read_result.error());
        if (*read_result == 0) return std::unexpected(SocketErrorInfo{SocketError::ReadError, "Connection closed"});
        pImpl_->read_buffer.append(temp_buf.data(), *read_result);
    }
}

} // namespace hfdown
