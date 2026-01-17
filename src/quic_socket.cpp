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
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <nghttp3/nghttp3.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <sys/select.h>
#endif

#if defined(USE_NGTCP2)

namespace hfdown {

static uint64_t quic_now_ns() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

static ngtcp2_conn* get_ngtcp2_conn_from_ref(ngtcp2_crypto_conn_ref* conn_ref) {
    return static_cast<QuicSocket*>(conn_ref->user_data)->ng_conn_;
}

static void rand_cb(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx*) {
    (void)RAND_bytes(dest, (int)destlen);
}

static int get_new_connection_id_cb(ngtcp2_conn*, ngtcp2_cid* cid, uint8_t* token, size_t cidlen, void*) {
    std::vector<uint8_t> data(cidlen);
    if (RAND_bytes(data.data(), (int)data.size()) != 1) return NGTCP2_ERR_CALLBACK_FAILURE;
    ngtcp2_cid_init(cid, data.data(), data.size());
    if (RAND_bytes(token, (int)NGTCP2_STATELESS_RESET_TOKENLEN) != 1) return NGTCP2_ERR_CALLBACK_FAILURE;
    return 0;
}

static int recv_stream_data_cb(ngtcp2_conn*, uint32_t flags, int64_t stream_id, uint64_t,
                               const uint8_t* data, size_t datalen, void* user_data, void*) {
    auto* s = static_cast<QuicSocket*>(user_data);
    if (!s->ng_h3conn_) return 0;
    int fin = (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0;
    nghttp3_tstamp ts = (nghttp3_tstamp)quic_now_ns();
    int rv = nghttp3_conn_read_stream2(s->ng_h3conn_, stream_id, data, datalen, fin, ts);
    if (rv != 0) return NGTCP2_ERR_CALLBACK_FAILURE;
    return 0;
}

}
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
#if defined(USE_NGTCP2)
    if (ng_h3conn_) {
        nghttp3_conn_del(ng_h3conn_);
        ng_h3conn_ = nullptr;
    }
    if (ng_conn_) {
        ngtcp2_conn_del(ng_conn_);
        ng_conn_ = nullptr;
    }
    if (ng_crypto_ctx_) {
        ngtcp2_crypto_ossl_ctx_del(reinterpret_cast<ngtcp2_crypto_ossl_ctx*>(ng_crypto_ctx_));
        ng_crypto_ctx_ = nullptr;
    }
    if (ng_ssl_) {
        SSL_set_app_data(ng_ssl_, nullptr);
        SSL_free(ng_ssl_);
        ng_ssl_ = nullptr;
    }
    if (ng_ssl_ctx_) {
        SSL_CTX_free(ng_ssl_ctx_);
        ng_ssl_ctx_ = nullptr;
    }
#endif
}

std::expected<void, QuicError> QuicSocket::connect(const std::string& host, uint16_t port) {
    std::cout << "[DEBUG] QuicSocket::connect host='" << host << "' port=" << port << "\n";
    peer_host_ = host;
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
    std::cout << "[DEBUG] init_quic: ngtcp2 enabled - initializing TLS helper\n";
    if (ngtcp2_crypto_ossl_init() != 0) {
        std::cerr << "[DEBUG] ngtcp2_crypto_ossl_init failed\n";
        return std::unexpected(QuicError{"ngtcp2_crypto_ossl_init failed", 0});
    }

    ng_ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ng_ssl_ctx_) {
        unsigned long e = ERR_get_error();
        return std::unexpected(QuicError{std::format("SSL_CTX_new failed: {}", e), (int)e});
    }
    SSL_CTX_set_min_proto_version(ng_ssl_ctx_, TLS1_3_VERSION);

    ng_ssl_ = SSL_new(ng_ssl_ctx_);
    if (!ng_ssl_) {
        SSL_CTX_free(ng_ssl_ctx_);
        ng_ssl_ctx_ = nullptr;
        unsigned long e = ERR_get_error();
        return std::unexpected(QuicError{std::format("SSL_new failed: {}", e), (int)e});
    }

    if (ngtcp2_crypto_ossl_ctx_new(reinterpret_cast<ngtcp2_crypto_ossl_ctx**>(&ng_crypto_ctx_), ng_ssl_) != 0) {
        SSL_free(ng_ssl_);
        ng_ssl_ = nullptr;
        SSL_CTX_free(ng_ssl_ctx_);
        ng_ssl_ctx_ = nullptr;
        return std::unexpected(QuicError{"ngtcp2_crypto_ossl_ctx_new failed", 0});
    }

    std::cout << "[DEBUG] ngtcp2 TLS helper initialized\n";
    // ngtcp2_conn and nghttp3_conn will be created in handshake
    ng_conn_ = nullptr;
    ng_h3conn_ = nullptr;
    ng_stream_id_ = 0;
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
    std::cout << "[DEBUG] handshake: ngtcp2/nghttp3 session creation\n";
    if (!ng_crypto_ctx_ || !ng_ssl_ || !ng_ssl_ctx_) {
        return std::unexpected(QuicError{"ngtcp2 TLS not initialized", 0});
    }

    // Configure TLS for QUIC (ALPN + SNI + glue)
    (void)SSL_set_tlsext_host_name(ng_ssl_, peer_host_.c_str());
    // ALPN wire format: list of (len, bytes)
    static const uint8_t alpn[] = {0x02, 'h', '3'};
    (void)SSL_set_alpn_protos(ng_ssl_, alpn, (unsigned)sizeof(alpn));
    SSL_set_connect_state(ng_ssl_);

    ng_conn_ref_.get_conn = get_ngtcp2_conn_from_ref;
    ng_conn_ref_.user_data = this;
    SSL_set_app_data(ng_ssl_, &ng_conn_ref_);

    if (ngtcp2_crypto_ossl_configure_client_session(ng_ssl_) != 0) {
        return std::unexpected(QuicError{"ngtcp2_crypto_ossl_configure_client_session failed", 0});
    }

    // Generate random DCID/SCID
    uint8_t dcidbuf[16];
    uint8_t scidbuf[16];
    if (RAND_bytes(dcidbuf, sizeof(dcidbuf)) != 1 || RAND_bytes(scidbuf, sizeof(scidbuf)) != 1) {
        return std::unexpected(QuicError{"RAND_bytes failed", 0});
    }
    ngtcp2_cid dcid;
    ngtcp2_cid scid;
    ngtcp2_cid_init(&dcid, dcidbuf, sizeof(dcidbuf));
    ngtcp2_cid_init(&scid, scidbuf, sizeof(scidbuf));

    // Path (local/remote)
    sockaddr_storage local_addr{};
    socklen_t local_addr_len = sizeof(local_addr);
    if (getsockname(udp_fd_, reinterpret_cast<sockaddr*>(&local_addr), &local_addr_len) != 0) {
        return std::unexpected(QuicError{"getsockname failed", errno});
    }
    ngtcp2_path path;
    memset(&path, 0, sizeof(path));
    ngtcp2_addr_init(&path.local, reinterpret_cast<const ngtcp2_sockaddr*>(&local_addr), (ngtcp2_socklen)local_addr_len);
    ngtcp2_addr_init(&path.remote, reinterpret_cast<const ngtcp2_sockaddr*>(&peer_addr_), (ngtcp2_socklen)peer_addr_len_);

    // ngtcp2 callbacks
    ngtcp2_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.client_initial = ngtcp2_crypto_client_initial_cb;
    callbacks.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
    callbacks.encrypt = ngtcp2_crypto_encrypt_cb;
    callbacks.decrypt = ngtcp2_crypto_decrypt_cb;
    callbacks.hp_mask = ngtcp2_crypto_hp_mask_cb;
    callbacks.recv_stream_data = recv_stream_data_cb;
    callbacks.recv_retry = ngtcp2_crypto_recv_retry_cb;
    callbacks.rand = rand_cb;
    callbacks.get_new_connection_id = get_new_connection_id_cb;
    callbacks.update_key = ngtcp2_crypto_update_key_cb;
    callbacks.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
    callbacks.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
    callbacks.get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb;
    callbacks.version_negotiation = ngtcp2_crypto_version_negotiation_cb;

    // ngtcp2 settings / transport params
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = (ngtcp2_tstamp)quic_now_ns();

    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    params.initial_max_stream_data_bidi_local = 1 << 20;
    params.initial_max_stream_data_bidi_remote = 1 << 20;
    params.initial_max_data = 1 << 20;
    params.initial_max_streams_bidi = 16;
    params.initial_max_streams_uni = 16;

    int rv = ngtcp2_conn_client_new(&ng_conn_, &dcid, &scid, &path, NGTCP2_PROTO_VER_V1,
                                    &callbacks, &settings, &params, nullptr, this);
    if (rv != 0 || !ng_conn_) {
        return std::unexpected(QuicError{"ngtcp2_conn_client_new failed", rv});
    }

    ngtcp2_conn_set_tls_native_handle(ng_conn_, ng_crypto_ctx_);

    // nghttp3 callbacks/settings
    nghttp3_callbacks h3_callbacks;
    memset(&h3_callbacks, 0, sizeof(h3_callbacks));
    nghttp3_settings h3_settings;
    nghttp3_settings_default(&h3_settings);

    rv = nghttp3_conn_client_new(&ng_h3conn_, &h3_callbacks, &h3_settings, nullptr, this);
    if (rv != 0 || !ng_h3conn_) {
        ngtcp2_conn_del(ng_conn_);
        ng_conn_ = nullptr;
        return std::unexpected(QuicError{"nghttp3_conn_client_new failed", rv});
    }

    // Drive basic handshake: send/recv packets until ngtcp2 reports handshake complete
    recv_buffer_.resize(65536);
    ngtcp2_pkt_info pi;
    memset(&pi, 0, sizeof(pi));
    pi.ecn = NGTCP2_ECN_NOT_ECT;

    for (int iter = 0; iter < 400 && !ngtcp2_conn_get_handshake_completed(ng_conn_); ++iter) {
        uint8_t out[1500];
        ngtcp2_ssize written = ngtcp2_conn_write_pkt(ng_conn_, &path, &pi, out, sizeof(out), (ngtcp2_tstamp)quic_now_ns());
        if (written > 0) {
            ssize_t s = ::sendto(udp_fd_, out, written, 0, reinterpret_cast<const struct sockaddr*>(&peer_addr_), peer_addr_len_);
            (void)s;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(udp_fd_, &readfds);
        struct timeval tv{0, 100000};
        int rv = select(udp_fd_ + 1, &readfds, NULL, NULL, &tv);
        if (rv > 0 && FD_ISSET(udp_fd_, &readfds)) {
            ssize_t recvd = ::recv(udp_fd_, recv_buffer_.data(), recv_buffer_.size(), 0);
            if (recvd > 0) {
                ngtcp2_ssize rc = ngtcp2_conn_read_pkt(ng_conn_, &path, &pi, reinterpret_cast<uint8_t*>(recv_buffer_.data()), static_cast<size_t>(recvd), (ngtcp2_tstamp)quic_now_ns());
                (void)rc;
            }
        }
    }

    if (!ngtcp2_conn_get_handshake_completed(ng_conn_)) {
        nghttp3_conn_del(ng_h3conn_);
        ng_h3conn_ = nullptr;
        ngtcp2_conn_del(ng_conn_);
        ng_conn_ = nullptr;
        return std::unexpected(QuicError{"QUIC handshake failed", 0});
    }

    connected_ = true;
    std::cout << "[DEBUG] ngtcp2/nghttp3 session created and handshake completed\n";
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
    // Minimal nghttp3: send a GET request
    if (!ng_h3conn_ || !ng_conn_) return std::unexpected(QuicError{"nghttp3/ngtcp2 not initialized", 0});

    // Prepare pseudo-headers for GET request
    std::vector<nghttp3_nv> nva;
    std::vector<std::string> names, values;
    for (const auto& [k, v] : headers) {
        names.push_back(k);
        values.push_back(v);
    }
    for (size_t i = 0; i < names.size(); ++i) {
        nghttp3_nv nv;
        nv.name = reinterpret_cast<const uint8_t*>(names[i].data());
        nv.namelen = names[i].size();
        nv.value = reinterpret_cast<const uint8_t*>(values[i].data());
        nv.valuelen = values[i].size();
        nv.flags = NGHTTP3_NV_FLAG_NONE;
        nva.push_back(nv);
    }

    // Open a new stream (stream_id = 0 for first request)
    ng_stream_id_ = 0;
    int rv = nghttp3_conn_submit_request(
        ng_h3conn_,
        ng_stream_id_,
        nva.data(),
        nva.size(),
        nullptr, // no data reader for GET
        nullptr  // stream user data
    );
    if (rv != 0) return std::unexpected(QuicError{"nghttp3_conn_submit_request failed", rv});

    // Drive ngtcp2/nghttp3 event loop to flush packets (basic loop)
    recv_buffer_.resize(65536);
    ngtcp2_pkt_info pi2;
    memset(&pi2, 0, sizeof(pi2));
    pi2.ecn = NGTCP2_ECN_NOT_ECT;
    // Run a short loop to emit packets and process incoming packets
    for (int iter = 0; iter < 60; ++iter) {
        uint8_t out[1500];
        ngtcp2_ssize written = ngtcp2_conn_write_pkt(ng_conn_, &path, &pi2, out, sizeof(out), (ngtcp2_tstamp)quic_now_ns());
        if (written > 0) {
            ssize_t s = ::sendto(udp_fd_, out, written, 0, reinterpret_cast<const struct sockaddr*>(&peer_addr_), peer_addr_len_);
            (void)s;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(udp_fd_, &readfds);
        struct timeval tv{0, 100000};
        int rv = select(udp_fd_ + 1, &readfds, NULL, NULL, &tv);
        if (rv > 0 && FD_ISSET(udp_fd_, &readfds)) {
            ssize_t recvd = ::recv(udp_fd_, recv_buffer_.data(), recv_buffer_.size(), 0);
            if (recvd > 0) {
                ngtcp2_ssize rc = ngtcp2_conn_read_pkt(ng_conn_, &path, &pi2, reinterpret_cast<uint8_t*>(recv_buffer_.data()), static_cast<size_t>(recvd), (ngtcp2_tstamp)quic_now_ns());
                (void)rc;
            }
        }
    }

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
    // Minimal nghttp3: poll for response headers
    if (!ng_h3conn_ || !ng_conn_) return std::unexpected(QuicError{"nghttp3/ngtcp2 not initialized", 0});

    // Basic implementation: read raw network bytes and return them (higher-level H3 parsing not yet implemented)
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
