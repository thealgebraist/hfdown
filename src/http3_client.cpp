#include "http3_client.hpp"
#include <format>
#include <algorithm>
#include <iostream>

namespace hfdown {

Http3Client::Http3Client() {
    // Set default headers for HTTP/3
    headers_["User-Agent"] = "hfdown-http3/1.0";
    headers_["Accept"] = "*/*";
}

void Http3Client::set_header(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void Http3Client::set_protocol(const std::string& protocol) {
    forced_protocol_ = protocol;
}

std::pair<std::string, uint16_t> Http3Client::parse_url(const std::string& url) {
    std::string host;
    uint16_t port = 443;
    auto host_start = url.find("://");
    if (host_start != std::string::npos) {
        host_start += 3;
    } else {
        host_start = 0;
    }
    auto path_start = url.find('/', host_start);
    auto host_part = path_start != std::string::npos ? url.substr(host_start, path_start - host_start) : url.substr(host_start);
    auto port_pos = host_part.find(':');
    if (port_pos != std::string::npos) {
        host = host_part.substr(0, port_pos);
        port = static_cast<uint16_t>(std::stoi(host_part.substr(port_pos + 1)));
    } else {
        host = host_part;
    }
    // Debug output
    std::cout << "[DEBUG] Parsed host: '" << host << "', port: " << port << "\n";
    return {host, port};
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::get(const std::string& url) {
    // Protocol negotiation order: HTTP/3 -> HTTP/2 -> HTTP/1.1
    if (forced_protocol_.empty() || forced_protocol_ == "h3") {
        auto result = try_http3(url);
        if (result || !forced_protocol_.empty()) return result;
    }
    
    if (forced_protocol_.empty() || forced_protocol_ == "h2") {
        auto result = try_http2(url);
        if (result || !forced_protocol_.empty()) return result;
    }
    
    return try_http1(url);
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::try_http3(const std::string& url) {
    auto [host, port] = parse_url(url);
    // Extract path (must be at least '/')
    std::string path = "/";
    auto host_start = url.find("://");
    if (host_start != std::string::npos) host_start += 3; else host_start = 0;
    auto path_start = url.find('/', host_start);
    if (path_start != std::string::npos) path = url.substr(path_start);
    std::cout << "[DEBUG] Using path: '" << path << "'\n";

    QuicSocket socket;
    std::cout << "[DEBUG] Connecting to host: '" << host << "', port: " << port << "\n";
    auto conn = socket.connect(host, port);
    if (!conn) {
        return std::unexpected(HttpErrorInfo{
            HttpError::ConnectionFailed,
            std::format("HTTP/3 connection failed: {}", conn.error().message)
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
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, send_result.error().message});
    }

    auto recv_result = socket.recv_headers();
    if (!recv_result) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, recv_result.error().message});
    }

    // Parse response (simplified - production would use QPACK)
    HttpResponse response{};
    response.status_code = 200; // Placeholder
    response.body = std::move(*recv_result);

    return std::expected<HttpResponse, HttpErrorInfo>(std::move(response));
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::try_http2(const std::string& url) {
    // HTTP/2 implementation would go here
    // For now, fall back to HTTP/1.1
    return std::unexpected(HttpErrorInfo{HttpError::ProtocolError, "HTTP/2 not yet implemented"});
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::try_http1(const std::string& url) {
    // Copy headers to HTTP/1.1 client
    for (const auto& [key, value] : headers_) {
        http1_fallback_.set_header(key, value);
    }
    
    auto result = http1_fallback_.get(url);
    if (!result) return std::unexpected(result.error());
    
    // Convert string response to HttpResponse with body
    HttpResponse response{};
    response.status_code = 200;
    response.body = std::move(*result);
    
    return std::expected<HttpResponse, HttpErrorInfo>(std::move(response));
}

std::expected<HttpResponse, HttpErrorInfo> Http3Client::get_with_range(
    const std::string& url, 
    size_t start, 
    size_t end
) {
    set_header("Range", std::format("bytes={}-{}", start, end));
    return get(url);
}

} // namespace hfdown
