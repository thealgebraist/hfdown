#include "quic_socket.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <format>
#include <iostream>
#include <cerrno>

#ifdef USE_QUIC
#include <quiche.h>
#include <openssl/rand.h>
#include <sys/select.h>
#endif

#ifdef USE_NGTCP2
#include <ngtcp2/ngtcp2.h>
#include <nghttp3/nghttp3.h>
#include <openssl/rand.h>
#include <sys/select.h>
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

    // Save peer sockaddr for quiche
    memset(&peer_addr_, 0, sizeof(peer_addr_));
    memcpy(&peer_addr_, result->ai_addr, result->ai_addrlen);
    peer_addr_len_ = static_cast<socklen_t>(result->ai_addrlen);

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
    // Initialize QUIC backend: prefer ngtcp2 -> quiche -> fallback
#if defined(USE_NGTCP2)
    std::cout << "[DEBUG] init_quic: ngtcp2 enabled (skeleton init)\n";
    // Minimal skeleton: real ngtcp2 integration is complex; mark as configured
    ngtcp2_session_ = reinterpret_cast<void*>(0x1);
    std::cout << "[DEBUG] ngtcp2_session_ -> " << ngtcp2_session_ << "\n";
    return {};
#elif defined(USE_QUIC)
    std::cout << "[DEBUG] init_quic: quiche available - creating config\n";
    config_ = (void*)quiche_config_new(QUICHE_PROTOCOL_VERSION);
    std::cout << "[DEBUG] quiche_config_new -> " << config_ << "\n";
    if (!config_) return std::unexpected(QuicError{"quiche_config_new failed", 0});

    std::cout << "[DEBUG] setting quiche config params\n";
    quiche_config_set_application_protos((quiche_config*)config_, (uint8_t*)"\x05h3-29", 6);
    quiche_config_set_max_idle_timeout((quiche_config*)config_, 5000);
    quiche_config_set_max_recv_udp_payload_size((quiche_config*)config_, 1350);
    quiche_config_set_max_send_udp_payload_size((quiche_config*)config_, 1350);
    quiche_config_set_initial_max_data((quiche_config*)config_, 10 * 1024 * 1024);
    quiche_config_set_initial_max_streams_bidi((quiche_config*)config_, 100);
    quiche_config_set_disable_active_migration((quiche_config*)config_, 1);
    std::cout << "[DEBUG] creating H3 config\n";
    h3_config_ = (void*)quiche_h3_config_new();
    std::cout << "[DEBUG] quiche_h3_config_new -> " << h3_config_ << "\n";
    if (!h3_config_) return std::unexpected(QuicError{"quiche_h3_config_new failed", 0});

    std::cout << "[DEBUG] init_quic: success\n";
    return {};
#else
    // Placeholder when no QUIC backend is enabled
    return {};
#endif
}

std::expected<void, QuicError> QuicSocket::handshake() {
    // Handshake depending on chosen backend
#if defined(USE_NGTCP2)
    std::cout << "[DEBUG] handshake: ngtcp2 skeleton - marking connected\n";
    if (!ngtcp2_session_) return std::unexpected(QuicError{"ngtcp2 not initialized", 0});
    connected_ = true;
    return {};
#elif defined(USE_QUIC)
    if (!config_ || !h3_config_) return std::unexpected(QuicError{"quiche not initialized", 0});

    // Create a random source connection id
    std::vector<uint8_t> scid(16);
    if (RAND_bytes(scid.data(), (int)scid.size()) != 1) return std::unexpected(QuicError{"RAND_bytes failed", 0});

    conn_ = (void*)quiche_connect(nullptr, scid.data(), scid.size(), nullptr, 0,
                                  (const struct sockaddr*)&peer_addr_, peer_addr_len_,
                                  (quiche_config*)config_);
    if (!conn_) return std::unexpected(QuicError{"quiche_connect failed", 0});

    recv_buffer_.resize(65536);

    // Handshake loop: send/recv until established or timeout
    for (int i = 0; i < 200 && !quiche_conn_is_established((quiche_conn*)conn_); ++i) {
        uint8_t out[1500];
        ssize_t written = quiche_conn_send((quiche_conn*)conn_, out, sizeof(out), nullptr);
        if (written > 0) {
            ssize_t s = ::send(udp_fd_, out, written, 0);
            (void)s;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(udp_fd_, &readfds);
        struct timeval tv{0, 100000}; // 100ms
        int rv = select(udp_fd_ + 1, &readfds, NULL, NULL, &tv);
        if (rv > 0 && FD_ISSET(udp_fd_, &readfds)) {
            ssize_t recvd = ::recv(udp_fd_, recv_buffer_.data(), recv_buffer_.size(), 0);
            if (recvd > 0) {
                ssize_t done = quiche_conn_recv((quiche_conn*)conn_, reinterpret_cast<uint8_t*>(recv_buffer_.data()), recvd, nullptr);
                (void)done;
            }
        }
    }

    if (!quiche_conn_is_established((quiche_conn*)conn_)) {
        return std::unexpected(QuicError{"QUIC handshake failed", 0});
    }

    // Create H3 connection
    h3_conn_ = (void*)quiche_h3_conn_new_with_transport((quiche_conn*)conn_, (quiche_h3_config*)h3_config_);
    if (!h3_conn_) return std::unexpected(QuicError{"quiche_h3_conn_new_with_transport failed", 0});

    connected_ = true;
    return {};
#else
    return {};
#endif
}

std::expected<size_t, QuicError> QuicSocket::send(std::span<const char> data) {
    if (!connected_) {
        return std::unexpected(QuicError{"Not connected", 0});
    }
    // If quiche is enabled, use quiche_conn_stream_send for H3 streams
#if defined(USE_NGTCP2)
    // Minimal ngtcp2 skeleton: send via UDP as a placeholder
    ssize_t sent = ::send(udp_fd_, data.data(), data.size(), 0);
    if (sent < 0) return std::unexpected(QuicError{"Send failed", errno});
    return static_cast<size_t>(sent);
#elif defined(USE_QUIC)
    if (h3_conn_) {
        uint64_t out_err = 0;
        ssize_t sent = quiche_conn_stream_send((quiche_conn*)conn_, h3_stream_id_, reinterpret_cast<const uint8_t*>(data.data()), data.size(), false, &out_err);
        if (sent < 0) return std::unexpected(QuicError{"quiche_conn_stream_send failed", (int)sent});

        // Flush packets
        uint8_t out[1500];
        ssize_t written = quiche_conn_send((quiche_conn*)conn_, out, sizeof(out), nullptr);
        if (written > 0) ::send(udp_fd_, out, written, 0);

        return static_cast<size_t>(sent);
    }
    return std::unexpected(QuicError{"H3 not initialized", 0});
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
    // If quiche is enabled, read using quiche APIs
#if defined(USE_NGTCP2)
    // Minimal ngtcp2 skeleton: read from UDP socket with timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(udp_fd_, &readfds);
    struct timeval tv{2, 0}; // 2s timeout
    int rv = select(udp_fd_ + 1, &readfds, NULL, NULL, &tv);
    if (rv == 0) return std::unexpected(QuicError{"Recv timeout", ETIMEDOUT});
    if (rv < 0) return std::unexpected(QuicError{"select failed", errno});
    if (FD_ISSET(udp_fd_, &readfds)) {
        ssize_t received = ::recv(udp_fd_, buffer.data(), buffer.size(), 0);
        if (received < 0) return std::unexpected(QuicError{"Recv failed", errno});
        return static_cast<size_t>(received);
    }
    return std::unexpected(QuicError{"No data", 0});
#elif defined(USE_QUIC)
    // Drive quiche: read network packets and poll H3 events
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(udp_fd_, &readfds);
    struct timeval tv{0, 500000};
    int rv = select(udp_fd_ + 1, &readfds, NULL, NULL, &tv);
    if (rv > 0 && FD_ISSET(udp_fd_, &readfds)) {
        ssize_t recvd = ::recv(udp_fd_, recv_buffer_.data(), recv_buffer_.size(), 0);
        if (recvd > 0) {
            quiche_conn_recv((quiche_conn*)conn_, reinterpret_cast<uint8_t*>(recv_buffer_.data()), recvd, nullptr);
        }
    }

    // Poll H3 events
    struct quiche_h3_event* ev = nullptr;
    int64_t poll_rc = quiche_h3_conn_poll((quiche_h3_conn*)h3_conn_, (quiche_conn*)conn_, &ev);
    if (poll_rc <= 0 || !ev) return std::unexpected(QuicError{"No H3 event", 0});

    if (quiche_h3_event_type(ev) == QUICHE_H3_EVENT_HEADERS) {
        std::string out;
        auto cb = [](uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp) -> int {
            std::string* o = reinterpret_cast<std::string*>(argp);
            o->append(reinterpret_cast<const char*>(name), name_len);
            o->append(": ");
            o->append(reinterpret_cast<const char*>(value), value_len);
            o->append("\n");
            return 0;
        };

            int rc = quiche_h3_event_for_each_header(ev, cb, &out);
        if (rc != 0) {
            quiche_h3_event_free(ev);
            return std::unexpected(QuicError{"Failed to iterate headers", rc});
        }

        size_t to_copy = std::min(buffer.size(), out.size());
        memcpy(buffer.data(), out.data(), to_copy);
        quiche_h3_event_free(ev);
        return to_copy;
    }

    quiche_h3_event_free(ev);
    return std::unexpected(QuicError{"Unhandled H3 event", 0});
#else
    // UDP fallback (demo only) with timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(udp_fd_, &readfds);
    struct timeval tv{2, 0}; // 2s timeout
    int rv = select(udp_fd_ + 1, &readfds, NULL, NULL, &tv);
    if (rv == 0) return std::unexpected(QuicError{"Recv timeout", ETIMEDOUT});
    if (rv < 0) return std::unexpected(QuicError{"select failed", errno});
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
    // Use quiche H3 API when available
#if defined(USE_NGTCP2)
    // ngtcp2 skeleton: fallback to sending headers as plain text over UDP
    std::string encoded_headers;
    for (const auto& [key, value] : headers) {
        encoded_headers += std::format("{}: {}\r\n", key, value);
    }
    encoded_headers += "\r\n";
    auto result = send(std::span{encoded_headers.data(), encoded_headers.size()});
    if (!result) return std::unexpected(result.error());
    return {};
#elif defined(USE_QUIC)
    if (!h3_conn_) return std::unexpected(QuicError{"H3 not initialized", 0});

    std::vector<quiche_h3_header> h3h;
    h3h.reserve(headers.size());
    std::vector<std::string> names, values;
    for (const auto& [k, v] : headers) {
        names.push_back(k);
        values.push_back(v);
    }
    for (size_t i = 0; i < names.size(); ++i) {
        quiche_h3_header hh;
        hh.name = reinterpret_cast<uint8_t*>(const_cast<char*>(names[i].data()));
        hh.name_len = names[i].size();
        hh.value = reinterpret_cast<uint8_t*>(const_cast<char*>(values[i].data()));
        hh.value_len = values[i].size();
        h3h.push_back(hh);
    }

    int64_t sid = quiche_h3_send_request((quiche_h3_conn*)h3_conn_, (quiche_conn*)conn_, h3h.data(), h3h.size(), 1);
    if (sid < 0) return std::unexpected(QuicError{"quiche_h3_send_request failed", (int)sid});
    h3_stream_id_ = static_cast<uint64_t>(sid);

    // Flush
    uint8_t out[1500];
    ssize_t written = quiche_conn_send((quiche_conn*)conn_, out, sizeof(out), nullptr);
    if (written > 0) ::send(udp_fd_, out, written, 0);

    return {};
#else
    // Fallback: encode headers as plain text
    std::string encoded_headers;
    for (const auto& [key, value] : headers) {
        encoded_headers += std::format("{}: {}\r\n", key, value);
    }
    encoded_headers += "\r\n";
    auto result = send(std::span{encoded_headers.data(), encoded_headers.size()});
    if (!result) return std::unexpected(result.error());
    return {};
#endif
}

std::expected<std::string, QuicError> QuicSocket::recv_headers() {
    // Use quiche H3 API when available
#if defined(USE_NGTCP2)
    recv_buffer_.resize(65536);
    auto res = recv(std::span{recv_buffer_.data(), recv_buffer_.size()});
    if (!res) return std::unexpected(res.error());
    return std::string(recv_buffer_.data(), *res);
#elif defined(USE_QUIC)
    recv_buffer_.resize(65536);
    auto res = recv(std::span{recv_buffer_.data(), recv_buffer_.size()});
    if (!res) return std::unexpected(res.error());
    return std::string(recv_buffer_.data(), *res);
#else
    // In production: Use QPACK decompression
    std::array<char, 8192> buffer;
    auto result = recv(std::span{buffer.data(), buffer.size()});
    if (!result) {
        return std::unexpected(result.error());
    }
    return std::string(buffer.data(), *result);
#endif
}

} // namespace hfdown
