#pragma once
#include <string>
#include <vector>
#include <span>
#include <expected>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef USE_NGTCP2
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <nghttp3/nghttp3.h>
#include <openssl/ssl.h>
#endif

namespace hfdown {

struct QuicError {
    std::string message;
    int code;
};

// HTTP/3 over QUIC socket wrapper
class QuicSocket {
public:
    QuicSocket();
    ~QuicSocket();
    
    std::expected<void, QuicError> connect(const std::string& host, uint16_t port);
    std::expected<size_t, QuicError> send(std::span<const char> data);
    std::expected<size_t, QuicError> recv(std::span<char> buffer);
    void close();
    
    bool is_connected() const { return connected_; }
    
    // HTTP/3 specific
    std::expected<void, QuicError> send_headers(const std::vector<std::pair<std::string, std::string>>& headers);
    std::expected<std::string, QuicError> recv_headers();
    
private:
    int udp_fd_;
    bool connected_;
    void* quic_conn_;  // Opaque pointer to QUIC connection (quiche)
    void* quic_stream_; // Opaque pointer to QUIC stream (for H3)
#ifdef USE_QUIC
    void* conn_ = nullptr;
    void* config_ = nullptr;
    void* h3_config_ = nullptr;
    void* h3_conn_ = nullptr;
    uint64_t h3_stream_id_ = 0;
#endif
#ifdef USE_NGTCP2
public:
    // ngtcp2/nghttp3 session state (public for callbacks)
    ::ngtcp2_conn* ng_conn_ = nullptr;
    ::nghttp3_conn* ng_h3conn_ = nullptr;
private:
    uint64_t ng_stream_id_ = 0;
    ::ngtcp2_crypto_conn_ref ng_conn_ref_{};
    // ngtcp2 OpenSSL glue (use global-qualified types)
    ::ngtcp2_crypto_ossl_ctx* ng_crypto_ctx_ = nullptr;
    ::SSL_CTX* ng_ssl_ctx_ = nullptr;
    ::SSL* ng_ssl_ = nullptr;
#endif
    std::vector<char> recv_buffer_;
    struct sockaddr_storage peer_addr_;
    socklen_t peer_addr_len_ = 0;
    std::string peer_host_;
    
    std::expected<void, QuicError> init_quic();
    std::expected<void, QuicError> handshake();
};

} // namespace hfdown
