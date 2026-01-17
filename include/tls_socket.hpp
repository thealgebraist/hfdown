#pragma once

#include "socket_wrapper.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace hfdown {

class TlsSocket : public ISocket {
public:
    TlsSocket();
    ~TlsSocket() override;
    
    TlsSocket(TlsSocket&&) noexcept;
    TlsSocket& operator=(TlsSocket&&) noexcept;
    
    TlsSocket(const TlsSocket&) = delete;
    TlsSocket& operator=(const TlsSocket&) = delete;
    
    std::expected<void, SocketErrorInfo> connect(const std::string& host, uint16_t port) override;
    std::expected<size_t, SocketErrorInfo> write(std::span<const char> data) override;
    std::expected<size_t, SocketErrorInfo> read(std::span<char> buffer) override;
    std::expected<std::string, SocketErrorInfo> read_until(const std::string& delimiter) override;
    
    void set_timeout(int seconds) override;
    void close() override;
    bool is_open() const override;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace hfdown
