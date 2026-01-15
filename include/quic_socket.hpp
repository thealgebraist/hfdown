#pragma once
#include <string>
#include <vector>
#include <span>
#include <expected>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <map>
#include <functional>

#ifdef USE_NGTCP2
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#ifdef USE_NGTCP2_CRYPTO_OSSL
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/ssl.h>
#elif defined(USE_NGTCP2_CRYPTO_GNUTLS)
#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <gnutls/gnutls.h>
#endif
#include <nghttp3/nghttp3.h>
#endif

namespace hfdown {

struct QuicError {
    std::string message;
    int code;
};

struct QuicResponse {
    int status_code;
    std::string body;
    std::map<std::string, std::string> headers;
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
    std::expected<QuicResponse, QuicError> get_response();
    
    // Set a callback for streaming data reception
    using DataCallback = std::function<void(int64_t stream_id, const uint8_t* data, size_t len)>;
    void set_data_callback(DataCallback cb) { data_callback_ = std::move(cb); }
    DataCallback data_callback_;

private:
    int udp_fd_;
    bool connected_;
    void* quic_conn_;  // Opaque pointer to QUIC connection
    void* quic_stream_; // Opaque pointer to QUIC stream (for H3)
#ifdef USE_NGTCP2
public:
    // ngtcp2/nghttp3 session state (public for callbacks)
    ::ngtcp2_conn* ng_conn_ = nullptr;
    ::nghttp3_conn* ng_h3conn_ = nullptr;
    std::map<int64_t, std::string> h3_headers_;
    std::map<int64_t, std::string> h3_bodies_;
    std::map<int64_t, bool> h3_stream_finished_;
    ::nghttp3_qpack_encoder* ng_qpack_encoder_ = nullptr;
    ::nghttp3_qpack_decoder* ng_qpack_decoder_ = nullptr;
private:
    uint64_t ng_stream_id_ = 0;
    ::ngtcp2_crypto_conn_ref ng_conn_ref_{};
    // ngtcp2 crypto glue
#ifdef USE_NGTCP2_CRYPTO_OSSL
    ::ngtcp2_crypto_ossl_ctx* ng_crypto_ctx_ = nullptr;
    ::SSL_CTX* ng_ssl_ctx_ = nullptr;
    ::SSL* ng_ssl_ = nullptr;
#elif defined(USE_NGTCP2_CRYPTO_GNUTLS)
    ::gnutls_session_t ng_gnutls_session_ = nullptr;
    ::gnutls_certificate_credentials_t ng_gnutls_cred_ = nullptr;
#endif
#endif
    std::vector<char> recv_buffer_;
    struct sockaddr_storage peer_addr_;
    socklen_t peer_addr_len_ = 0;
    std::string peer_host_;
    
    std::expected<void, QuicError> init_quic();
    std::expected<void, QuicError> handshake();
    void drive();
};

} // namespace hfdown
