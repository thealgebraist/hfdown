#pragma once

#include <string>
#include <expected>
#include <span>
#include <memory>
#include <optional>

namespace hfdown {

enum class SocketError { ConnectionFailed, ReadError, WriteError, TimeoutError, DNSError };

struct SocketErrorInfo {
    SocketError error;
    std::string message;
};

class Socket {
public:
    Socket();
    ~Socket();
    
    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;
    
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    std::expected<void, SocketErrorInfo> connect(const std::string& host, uint16_t port);
    std::expected<size_t, SocketErrorInfo> write(std::span<const char> data);
    std::expected<size_t, SocketErrorInfo> read(std::span<char> buffer);
    std::expected<std::string, SocketErrorInfo> read_until(const std::string& delimiter);
    
    void set_timeout(int seconds);
    void close();
    bool is_open() const;
    int fd() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace hfdown
