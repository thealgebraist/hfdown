#pragma once
#include <string>
#include <vector>
#include <span>
#include <expected>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>

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
    void* quic_conn_;  // Opaque pointer to QUIC connection
    void* quic_stream_; // Opaque pointer to QUIC stream
    std::vector<char> recv_buffer_;
    
    std::expected<void, QuicError> init_quic();
    std::expected<void, QuicError> handshake();
};

} // namespace hfdown
