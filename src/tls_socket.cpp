#include "tls_socket.hpp"
#include <format>

namespace hfdown {

class TlsSocket::Impl {
public:
    Socket socket;
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    std::string read_buffer;
    
    Impl() {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        ctx = SSL_CTX_new(TLS_client_method());
    }
    
    ~Impl() {
        if (ssl) SSL_free(ssl);
        if (ctx) SSL_CTX_free(ctx);
    }
};

TlsSocket::TlsSocket() : pImpl_(std::make_unique<Impl>()) {}
TlsSocket::~TlsSocket() = default;
TlsSocket::TlsSocket(TlsSocket&&) noexcept = default;
TlsSocket& TlsSocket::operator=(TlsSocket&&) noexcept = default;

void TlsSocket::set_timeout(int seconds) {
    pImpl_->socket.set_timeout(seconds);
}

bool TlsSocket::is_open() const {
    return pImpl_->ssl != nullptr && pImpl_->socket.is_open();
}

void TlsSocket::close() {
    if (pImpl_->ssl) {
        SSL_shutdown(pImpl_->ssl);
        SSL_free(pImpl_->ssl);
        pImpl_->ssl = nullptr;
    }
    pImpl_->socket.close();
}

std::expected<void, SocketErrorInfo> TlsSocket::connect(const std::string& host, uint16_t port) {
    auto conn = pImpl_->socket.connect(host, port);
    if (!conn) return conn;
    
    pImpl_->ssl = SSL_new(pImpl_->ctx);
    SSL_set_fd(pImpl_->ssl, pImpl_->socket.fd());
    SSL_set_tlsext_host_name(pImpl_->ssl, host.c_str());
    
    if (SSL_connect(pImpl_->ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        return std::unexpected(SocketErrorInfo{SocketError::ConnectionFailed, "TLS handshake failed"});
    }
    
    return {};
}

std::expected<size_t, SocketErrorInfo> TlsSocket::write(std::span<const char> data) {
    if (!pImpl_->ssl) {
        return std::unexpected(SocketErrorInfo{SocketError::WriteError, "TLS not connected"});
    }
    
    int sent = SSL_write(pImpl_->ssl, data.data(), data.size());
    if (sent <= 0) {
        return std::unexpected(SocketErrorInfo{SocketError::WriteError, 
            std::format("TLS write failed: {}", SSL_get_error(pImpl_->ssl, sent))});
    }
    return static_cast<size_t>(sent);
}

std::expected<size_t, SocketErrorInfo> TlsSocket::read(std::span<char> buffer) {
    if (!pImpl_->ssl) {
        return std::unexpected(SocketErrorInfo{SocketError::ReadError, "TLS not connected"});
    }
    
    if (!pImpl_->read_buffer.empty()) {
        size_t to_copy = std::min(buffer.size(), pImpl_->read_buffer.size());
        std::copy_n(pImpl_->read_buffer.begin(), to_copy, buffer.data());
        pImpl_->read_buffer.erase(0, to_copy);
        return to_copy;
    }
    
    int received = SSL_read(pImpl_->ssl, buffer.data(), buffer.size());
    if (received <= 0) {
        int err = SSL_get_error(pImpl_->ssl, received);
        if (err == SSL_ERROR_ZERO_RETURN) return 0;
        return std::unexpected(SocketErrorInfo{SocketError::ReadError, 
            std::format("TLS read failed: {}", err)});
    }
    return static_cast<size_t>(received);
}

std::expected<std::string, SocketErrorInfo> TlsSocket::read_until(const std::string& delim) {
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
