#pragma once
#include "quic_socket.hpp"
#include "http_client.hpp"
#include "http_protocol.hpp"
#include <string>
#include <expected>
#include <vector>
#include <map>

namespace hfdown {

// HTTP/3 client with automatic fallback to HTTP/2 and HTTP/1.1
class Http3Client {
public:
    Http3Client();
    
    // Auto-negotiates protocol version (HTTP/3 -> HTTP/2 -> HTTP/1.1)
    std::expected<HttpResponse, HttpErrorInfo> get(std::string_view url);
    
    std::expected<HttpResponse, HttpErrorInfo> get_with_range(
        std::string_view url, 
        size_t start, 
        size_t end
    );

    std::expected<void, HttpErrorInfo> download_file(
        std::string_view url,
        const std::filesystem::path& output_path,
        ProgressCallback progress_callback = nullptr,
        size_t resume_offset = 0,
        std::string_view expected_checksum = "",
        size_t write_offset = 0
    );
    
    void set_header(std::string_view key, std::string_view value);
    void set_config(const HttpConfig& config);
    
    // Force specific protocol version
    void set_protocol(std::string_view protocol); // "h3", "h2", "http/1.1"
    
    // Connection pooling and multiplexing
    void enable_multiplexing(bool enable) { multiplexing_enabled_ = enable; }
    void set_max_streams(size_t max) { max_concurrent_streams_ = max; }
    std::pair<std::string, uint16_t> parse_url(std::string_view url);
    
private:
    std::map<std::string, std::string> headers_;
    std::string forced_protocol_;
    bool multiplexing_enabled_ = true;
    size_t max_concurrent_streams_ = 100;
    
    HttpClient http1_fallback_;
    
    // Persistent protocol discovery cache (host -> protocol)
    inline static std::map<std::string, std::string> protocol_cache_;
    
    std::expected<HttpResponse, HttpErrorInfo> try_http3(std::string_view url);
    std::expected<HttpResponse, HttpErrorInfo> try_http2(std::string_view url);
    std::expected<HttpResponse, HttpErrorInfo> try_http1(std::string_view url);
    
};

} // namespace hfdown
