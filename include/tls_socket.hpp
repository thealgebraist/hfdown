#pragma once

#include "socket_wrapper.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace hfdown {

class TlsSocket {
public:
    TlsSocket();
    ~TlsSocket();
    
    TlsSocket(TlsSocket&&) noexcept;
    TlsSocket& operator=(TlsSocket&&) noexcept;
    
    TlsSocket(const TlsSocket&) = delete;
    TlsSocket& operator=(const TlsSocket&) = delete;
    
    std::expected<void, SocketErrorInfo> connect(const std::string& host, uint16_t port);
    std::expected<size_t, SocketErrorInfo> write(std::span<const char> data);
    std::expected<size_t, SocketErrorInfo> read(std::span<char> buffer);
    std::expected<std::string, SocketErrorInfo> read_until(const std::string& delimiter);
    
    void set_timeout(int seconds);
    void close();
    bool is_open() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace hfdown
