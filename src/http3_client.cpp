#include "http3_client.hpp"
#include "compact_log.hpp"
#include <charconv>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <mutex>
#include <format>

namespace hfdown {

Http3Client::Http3Client() {
    // Set default headers for HTTP/3
    headers_["User-Agent"] = "hfdown-http3/1.0";
    headers_["Accept"] = "*/*";
}

std::expected<void, HttpErrorInfo> Http3Client::download_file(
    std::string_view url,
    const std::filesystem::path& output_path,
    ProgressCallback progress_callback,
    size_t resume_offset,
    std::string_view expected_checksum,
    size_t write_offset
) {
    if (url.starts_with("http://")) {
        compact::Writer::error("[H3] download_file falling back to H2C\n");
        // Copy headers
        for (const auto& [k, v] : headers_) http1_fallback_.set_header(k, v);
        return http1_fallback_.download_file(url, output_path, progress_callback, resume_offset, expected_checksum, write_offset);
    }

    auto [host, port] = parse_url(url);
    bool use_h3 = (forced_protocol_ == "h3");
    
    // Check if we already know this host supports H3
    if (!use_h3 && forced_protocol_.empty()) {
        static std::mutex cache_mutex;
        std::lock_guard lock(cache_mutex);
        if (auto it = protocol_cache_.find(host); it != protocol_cache_.end() && it->second == "h3") {
            use_h3 = true;
        }
    }

    if (use_h3) {
        std::string path = "/";
        auto host_start = url.find("://");
        if (host_start != std::string_view::npos) host_start += 3; else host_start = 0;
        auto path_start = url.find('/', host_start);
        if (path_start != std::string_view::npos) path = std::string(url.substr(path_start));

        QuicSocket socket;
        auto conn = socket.connect(host, port);
        if (!conn) {
            if (forced_protocol_ == "h3") {
                return std::unexpected(HttpErrorInfo{HttpError::ConnectionFailed, "H3 Conn Failed"});
            }
            goto fallback;
        }

        std::ofstream file(output_path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) {
            // Fallback if file doesn't exist yet
            file.open(output_path, std::ios::binary | std::ios::out);
        }
        if (!file) return std::unexpected(HttpErrorInfo{HttpError::FileWriteError, "Failed to open output file"});
        if (write_offset > 0) file.seekp(write_offset);

        size_t downloaded = 0;
        socket.set_data_callback([&](int64_t, const uint8_t* data, size_t len) {
            file.write((const char*)data, len);
            downloaded += len;
            if (progress_callback) {
                DownloadProgress p;
                p.downloaded_bytes = downloaded;
                p.total_bytes = 0; // Unknown from H3 for now
                progress_callback(p);
            }
        });

        std::vector<std::pair<std::string, std::string>> h3_headers;
        h3_headers.emplace_back(":method", "GET");
        h3_headers.emplace_back(":scheme", "https");
        h3_headers.emplace_back(":authority", host);
        h3_headers.emplace_back(":path", path);
        for (const auto& [k, v] : headers_) h3_headers.emplace_back(k, v);

        if (socket.send_headers(h3_headers)) {
            auto resp = socket.get_response();
            if (resp && resp->status_code < 400) return {};
        }
        if (forced_protocol_ == "h3") return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "H3 download failed"});
    }

fallback:
    // Fallback to HTTP/1.1 for streaming (this is memory-efficient)
    for (const auto& [key, value] : headers_) {
        http1_fallback_.set_header(key, value);
    }
    
    return http1_fallback_.download_file(url, output_path, progress_callback, resume_offset, expected_checksum, write_offset);
}

void Http3Client::set_header(std::string_view key, std::string_view value) {
    headers_[std::string(key)] = std::string(value);
}

void Http3Client::set_config(const HttpConfig& config) {
    http1_fallback_.set_config(config);
}

void Http3Client::set_protocol(std::string_view protocol) {
    forced_protocol_ = protocol;
}

std::pair<std::string, uint16_t> Http3Client::parse_url(std::string_view url) {
    std::string host;
    uint16_t port = 443;
    auto host_start = url.find("://");
    if (host_start != std::string_view::npos) {
        host_start += 3;
    } else {
        host_start = 0;
    }
    auto path_start = url.find('/', host_start);
    auto host_part = path_start != std::string_view::npos ? url.substr(host_start, path_start - host_start) : url.substr(host_start);
    auto port_pos = host_part.find(':');
    if (port_pos != std::string_view::npos) {
        host = std::string(host_part.substr(0, port_pos));
        std::string port_str(host_part.substr(port_pos + 1));
        std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
    } else {
        host = std::string(host_part);
    }
    return {host, port};
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::get(std::string_view url) {
    auto [host, port] = parse_url(url);
    
    // http:// must use try_http2 (h2c) as QUIC requires TLS
    if (url.starts_with("http://")) {
        return try_http2(url);
    }

    // If protocol is forced, use it immediately
    if (!forced_protocol_.empty()) {
        if (forced_protocol_ == "h3") return try_http3(url);
        if (forced_protocol_ == "h2") return try_http2(url);
        return try_http1(url);
    }

    // Check cache: if we ALREADY know it supports H3, try it
    {
        // Simple process-local cache
        static std::mutex cache_mutex;
        std::lock_guard lock(cache_mutex);
        if (auto it = protocol_cache_.find(host); it != protocol_cache_.end()) {
            if (it->second == "h3") {
                auto result = try_http3(url);
                if (result) return result;
                protocol_cache_.erase(host);
            }
        }
    }

    // Default: Try HTTP/1.1 first and look for Alt-Svc
    auto result = try_http1(url);
    if (result) {
        if (!result->alt_svc.empty()) {
            if (result->alt_svc.find("h3") != std::string::npos) {
                protocol_cache_[host] = "h3";
            }
        }
        if (result->status_code >= 400) {
            return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP Error {}", result->status_code), result->status_code});
        }
    }
    return result;
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::try_http3(std::string_view url) {
    auto [host, port] = parse_url(url);
    // Extract path (must be at least '/')
    std::string path = "/";
    auto host_start = url.find("://");
    if (host_start != std::string_view::npos) host_start += 3; else host_start = 0;
    auto path_start = url.find('/', host_start);
    if (path_start != std::string_view::npos) path = std::string(url.substr(path_start));

    QuicSocket socket;
    auto conn = socket.connect(host, port);
    if (!conn) {
        return std::unexpected(HttpErrorInfo{
            HttpError::ConnectionFailed,
            "H3 connection failed"
        });
    }

    // Build HTTP/3 request headers
    std::vector<std::pair<std::string, std::string>> h3_headers;
    h3_headers.emplace_back(":method", "GET");
    h3_headers.emplace_back(":scheme", "https");
    h3_headers.emplace_back(":authority", host);
    h3_headers.emplace_back(":path", path);

    for (const auto& [key, value] : headers_) {
        h3_headers.emplace_back(key, value);
    }

    auto send_result = socket.send_headers(h3_headers);
    if (!send_result) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Send failed"});
    }

    auto resp_result = socket.get_response();
    if (!resp_result) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Recv failed"});
    }

    HttpResponse response{};
    response.status_code = resp_result->status_code;
    response.body = std::move(resp_result->body);
    response.protocol = "h3";
    
    if (response.status_code >= 400) {
        return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP Error {}", response.status_code), response.status_code});
    }

    return response;
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::try_http2(std::string_view url) {
    // Copy headers to HTTP client
    for (const auto& [key, value] : headers_) {
        http1_fallback_.set_header(key, value);
    }
    
    auto result = http1_fallback_.get_full(url);
    if (!result) return std::unexpected(result.error());
    
    // HttpClient (via curl) will negotiate H2 if supported
    return result;
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::try_http1(std::string_view url) {
    // Copy headers to HTTP/1.1 client
    for (const auto& [key, value] : headers_) {
        http1_fallback_.set_header(key, value);
    }
    
    auto result = http1_fallback_.get_full(url);
    if (!result) return std::unexpected(result.error());
    
    return result;
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::get_with_range(
    std::string_view url, 
    size_t start, 
    size_t end
) {
    char range_val[64];
    auto [ptr, ec] = std::to_chars(range_val, range_val + sizeof(range_val), start);
    std::string rv = "bytes=";
    rv.append(range_val, ptr - range_val);
    rv += "-";
    auto [ptr2, ec2] = std::to_chars(range_val, range_val + sizeof(range_val), end);
    rv.append(range_val, ptr2 - range_val);

    set_header("Range", rv);
    auto res = get(url);
    // Important: remove range header so subsequent requests aren't affected
    headers_.erase("Range");
    return res;
}

} // namespace hfdown