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

class ISocket {
public:
    virtual ~ISocket() = default;
    virtual std::expected<void, SocketErrorInfo> connect(const std::string& host, uint16_t port) = 0;
    virtual std::expected<size_t, SocketErrorInfo> write(std::span<const char> data) = 0;
    virtual std::expected<size_t, SocketErrorInfo> read(std::span<char> buffer) = 0;
    virtual std::expected<std::string, SocketErrorInfo> read_until(const std::string& delimiter) = 0;
    virtual void set_timeout(int seconds) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
};

class Socket : public ISocket {
public:
    Socket();
    ~Socket() override;
    
    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;
    
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    std::expected<void, SocketErrorInfo> connect(const std::string& host, uint16_t port) override;
    std::expected<size_t, SocketErrorInfo> write(std::span<const char> data) override;
    std::expected<size_t, SocketErrorInfo> read(std::span<char> buffer) override;
    std::expected<std::string, SocketErrorInfo> read_until(const std::string& delimiter) override;
    
    void set_timeout(int seconds) override;
    void close() override;
    bool is_open() const override;
    int fd() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace hfdown
