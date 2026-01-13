#include "quic_socket.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <format>
#include <iostream>

#ifdef USE_QUIC
#include <quiche.h>
#endif

namespace hfdown {

QuicSocket::QuicSocket() 
    : udp_fd_(-1), connected_(false), quic_conn_(nullptr), quic_stream_(nullptr) {
    recv_buffer_.reserve(65536);
}

QuicSocket::~QuicSocket() {
    close();
}

void QuicSocket::close() {
    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
    }
    connected_ = false;
}

std::expected<void, QuicError> QuicSocket::connect(const std::string& host, uint16_t port) {
    std::cout << "[DEBUG] QuicSocket::connect host='" << host << "' port=" << port << "\n";
    udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd_ < 0) {
        return std::unexpected(QuicError{"Failed to create UDP socket", errno});
    }

    struct addrinfo hints{}, *result;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        std::cerr << "[DEBUG] getaddrinfo failed for host '" << host << "' port " << port << "\n";
        return std::unexpected(QuicError{"Failed to resolve host", errno});
    }

    if (::connect(udp_fd_, result->ai_addr, result->ai_addrlen) < 0) {
        freeaddrinfo(result);
        std::cerr << "[DEBUG] ::connect failed for host '" << host << "' port " << port << "\n";
        return std::unexpected(QuicError{"Failed to connect", errno});
    }

    freeaddrinfo(result);

    // Initialize QUIC connection (to be replaced with real QUIC library)
    auto quic_init = init_quic();
    if (!quic_init) return std::unexpected(quic_init.error());

    auto hs = handshake();
    if (!hs) return std::unexpected(hs.error());

    connected_ = true;
    return {};
}

std::expected<void, QuicError> QuicSocket::init_quic() {
    // Initialize quiche if available
#ifdef USE_QUIC
    // Minimal quiche config creation (simplified)
    // Real implementation should create UDP socket, QUIC connection, and set up TLS.
    std::cout << "[DEBUG] init_quic: quiche available\n";
    return {};
#else
    // Placeholder for QUIC initialization when quiche unavailable
    return {};
#endif
}

std::expected<void, QuicError> QuicSocket::handshake() {
    // Perform handshake using quiche if available
#ifdef USE_QUIC
    std::cout << "[DEBUG] handshake: quiche handshake placeholder\n";
    return {};
#else
    // Placeholder when no quiche
    return {};
#endif
}

std::expected<size_t, QuicError> QuicSocket::send(std::span<const char> data) {
    if (!connected_) {
        return std::unexpected(QuicError{"Not connected", 0});
    }
    // If quiche is enabled, would feed bytes into quiche connection
#ifdef USE_QUIC
    // TODO: implement quiche send
    std::cout << "[DEBUG] quiche send placeholder, bytes=" << data.size() << "\n";
    return static_cast<size_t>(data.size());
#else
    // UDP fallback (demo only)
    ssize_t sent = ::send(udp_fd_, data.data(), data.size(), 0);
    if (sent < 0) {
        return std::unexpected(QuicError{"Send failed", errno});
    }
    return static_cast<size_t>(sent);
#endif
}

std::expected<size_t, QuicError> QuicSocket::recv(std::span<char> buffer) {
    if (!connected_) {
        return std::unexpected(QuicError{"Not connected", 0});
    }
    // If quiche is enabled, would read from quiche connection
#ifdef USE_QUIC
    // TODO: implement quiche recv
    std::string demo = "HTTP/3 demo response body";
    size_t to_copy = std::min(buffer.size(), demo.size());
    memcpy(buffer.data(), demo.data(), to_copy);
    return to_copy;
#else
    // UDP fallback (demo only)
    ssize_t received = ::recv(udp_fd_, buffer.data(), buffer.size(), 0);
    if (received < 0) {
        return std::unexpected(QuicError{"Recv failed", errno});
    }
    return static_cast<size_t>(received);
#endif
}

std::expected<void, QuicError> QuicSocket::send_headers(
    const std::vector<std::pair<std::string, std::string>>& headers
) {
    // In production: Use QPACK compression
    std::string encoded_headers;
    
    for (const auto& [key, value] : headers) {
        encoded_headers += std::format("{}: {}\r\n", key, value);
    }
    encoded_headers += "\r\n";
    
    auto result = send(std::span{encoded_headers.data(), encoded_headers.size()});
    if (!result) return std::unexpected(result.error());
    
    return {};
}

std::expected<std::string, QuicError> QuicSocket::recv_headers() {
    // In production: Use QPACK decompression
    std::array<char, 8192> buffer;
    auto result = recv(std::span{buffer.data(), buffer.size()});
    
    if (!result) {
        return std::unexpected(result.error());
    }
    
    return std::string(buffer.data(), *result);
}

} // namespace hfdown
