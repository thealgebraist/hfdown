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
    std::expected<HttpResponse, HttpErrorInfo> get(const std::string& url);
    
    std::expected<HttpResponse, HttpErrorInfo> get_with_range(
        const std::string& url, 
        size_t start, 
        size_t end
    );

    std::expected<void, HttpErrorInfo> download_file(
        const std::string& url,
        const std::filesystem::path& output_path,
        ProgressCallback progress_callback = nullptr,
        size_t resume_offset = 0,
        const std::string& expected_checksum = "",
        size_t write_offset = 0
    );
    
    void set_header(const std::string& key, const std::string& value);
    void set_config(const HttpConfig& config);
    
    // Force specific protocol version
    void set_protocol(const std::string& protocol); // "h3", "h2", "http/1.1"
    
    // Connection pooling and multiplexing
    void enable_multiplexing(bool enable) { multiplexing_enabled_ = enable; }
    void set_max_streams(size_t max) { max_concurrent_streams_ = max; }
    std::pair<std::string, uint16_t> parse_url(const std::string& url);
    
private:
    std::map<std::string, std::string> headers_;
    std::string forced_protocol_;
    bool multiplexing_enabled_ = true;
    size_t max_concurrent_streams_ = 100;
    
    HttpClient http1_fallback_;
    
    // Persistent protocol discovery cache (host -> protocol)
    inline static std::map<std::string, std::string> protocol_cache_;
    
    std::expected<HttpResponse, HttpErrorInfo> try_http3(const std::string& url);
    std::expected<HttpResponse, HttpErrorInfo> try_http2(const std::string& url);
    std::expected<HttpResponse, HttpErrorInfo> try_http1(const std::string& url);
    
};

} // namespace hfdown
