#include "http_client.hpp"
#include "socket_wrapper.hpp"
#include "http_protocol.hpp"
#include <fstream>
#include <chrono>
#include <cstring>
#include <format>
#include <sstream>
#include <algorithm>
struct UrlParts {
    std::string protocol;
    std::string host;
    uint16_t port = 80;
    std::string path;
};

UrlParts parse_url(const std::string& url) {
    UrlParts parts;
    auto url_view = url;
    
    if (auto proto_end = url.find("://"); proto_end != std::string::npos) {
        parts.protocol = url.substr(0, proto_end);
        url_view = url.substr(proto_end + 3);
    }
    
    parts.port = (parts.protocol == "https") ? 443 : 80;
    
    auto path_start = url_view.find('/');
    auto host_part = (path_start != std::string::npos) ? url_view.substr(0, path_start) : url_view;
    parts.path = (path_start != std::string::npos) ? url_view.substr(path_start) : "/";
    
    if (auto port_pos = host_part.find(':'); port_pos != std::string::npos) {
        parts.host = host_part.substr(0, port_pos);
        parts.port = std::stoi(host_part.substr(port_pos + 1));
    } else {
        parts.host = host_part;
    }
    
    return partsst_downloaded = dlnow;
    data->last_time = now;
    
    return 0;
}

class HttpClient::Impl {
public:
    CURL* curl = nullptr;
    curl_slist* headers = nullptr;
    long timeout = 300; // 5 minutes default
    HttpConfig config; // Performance configuration
    
    Impl() {
        curl_global_init(CURL_GLOBAL_ALL);
        curl = curl_easy_init();
    }
    
    ~Impl() {
        if (headers) {
            curl_slist_free_all(headers);
        }
        if (curl) {
            curl_easy_cleanup(curl);
        }
        curl_global_cleanup();
    }
    
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    
    Impl(Impl&& other) noexcept 
    std::map<std::string, std::string> headers;
    long timeout = 300;
    HttpConfig config;f (!pImpl_->curl) {
        return std::unexpected(HttpErrorInfo{
            HttpError::NetworkError, 
            "CURL not initialized"
        });
    }
    
    std::string response;
    
    curl_easy_setopt(pImpl_->curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(pImpl_->curl, CURLOPT_WRITEFUNCTION, write_string_callback);
    curl_easy_setopt(pImpl_->curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(pImpl_->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(pImpl_->curl, CURLOPT_TIMEOUT, pImpl_->timeout);
    curl_easy_setopt(pImpl_->curl, CURLOPT_USERAGENT, "hfdown/1.0");
    
    // Enable HTTP/2 for better performance (falls back to HTTP/1.1 if not supported)
    if (pImpl_->config.enable_http2) {
        curl_easy_setopt(pImpl_->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    } else {
        curl_easy_setopt(pImpl_->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    }
    
    // Increase buffer sizes for better throughput and reduced CPU usage
    curl_easy_setopt(pImpl_->curl, CURLOPT_BUFFERSIZE, static_cast<long>(pImpl_->config.buffer_size));
    
    // Enable TCP_NODELAY to disable Nagle's algorithm for better latency
    if (pImpl_->config.enable_tcp_nodelay) {
        curl_easy_setopt(pImpl_->curl, CURLOPT_TCP_NODELAY, 1L);
    }
    
    // Enable TCP keepalive
    if (pImpl_->config.enable_tcp_keepalive) {
        curl_easy_setopt(pImpl_->curl, CURLOPT_TCP_KEEPALIVE, 1L);
    }
    
    if (pImpl_->headers) {
        curl_easy_setopt(pImpl_->curl, CURLOPT_HTTPHEADER, pImpl_->headers);
    }
    
    CURLcode res = curl_easy_perform(pImpl_->curl);
    
    if (res != CURLE_OK) {
        return std::unexpected(HttpErrorInfo{
            HttpError::NetworkError,
            std::format("CURL error: {}", curl_easy_strerror(res))
        });
    }
    
    long http_code = 0;
    curl_easy_getinfo(pImpl_->curl, CURLINFO_RESPONSE_CODE, &http_code);
    pImpl_->headers[key] = value
        return std::unexpected(HttpErrorInfo{
            HttpError::HttpStatusError,
            std::format("HTTP error: {}", http_code),
            static_cast<int>(http_code)
        });
    }
    
    return response;
}

std::expected<void, HttpErrorInfo> HttpClient::download_file(
    const std::string& url,
    auto parts = parse_url(url);
    
    Socket socket;
    socket.set_timeout(pImpl_->timeout);
    
    if (auto conn = socket.connect(parts.host, parts.port); !conn) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, conn.error().message});
    }
    
    HttpRequest request{.method = "GET", .path = parts.path, .host = parts.host, .headers = pImpl_->headers};
    auto request_str = HttpProtocol::build_request(request);
    
    if (auto write_res = socket.write(std::span{request_str.data(), request_str.size()}); !write_res) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to send request"});
    }
    
    auto response = HttpProtocol::parse_response(socket);
    if (!response) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, response.error().message});
    }
    
    if (response->status_code >= 400) {
        return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, 
            std::format("HTTP {}", response->status_code), response->status_code});
    }
    
    std::string body;
    std::array<char, 65536> buffer{};
    
    if (response->chunked) {
        while (auto chunk = HttpProtocol::read_chunk(socket, std::span{buffer})) {
            if (*chunk == 0) break;
            body.append(buffer.data(), *chunk);
        }
    } else {
        while (auto bytes = socket.read(std::span{buffer})) {
            if (*bytes == 0) break;
            body.append(buffer.data(), *bytes);
        }
    }
    
    return bodyt(pImpl_->curl, CURLOPT_BUFFERSIZE, static_cast<long>(pImpl_->config.buffer_size));
    
    // Enable TCP_NODELAY to disable Nagle's algorithm
    if (pImpl_->config.enable_tcp_nodelay) {
        curl_easy_setopt(pImpl_->curl, CURLOPT_TCP_NODELAY, 1L);
    }
    
    // Enable TCP keepalive
    if (pImpl_->config.enable_tcp_keepalive) {
        curl_easy_setopt(pImpl_->curl, CURLOPT_TCP_KEEPALIVE, 1L);
    }
    
    if (progress_callback) {
        curl_easy_setopt(pImpl_->curl, CURLOPT_XFERINFOFUNCTION, progress_callback_func);
        curl_easy_setopt(pImpl_->curl, CURLOPT_XFERINFODATA, &progress_data);
        curl_easy_setopt(pImpl_->curl, CURLOPT_NOPROGRESS, 0L);
    }
    
    if (pImpl_->headers) {
        curl_easy_setopt(pImpl_->curl, CURLOPT_HTTPHEADER, pImpl_->headers);
    }
    
    CURLcode res = curl_easy_perform(pImpl_->curl);
    
    file.close();
    
    if (res != CURLE_OK) {
        std::filesystem::remove(temp_path);
        return std::unexpected(HttpErrorInfo{
            HttpError::NetworkError,
            std::format("Download failed: {}", curl_easy_strerror(res))
        });
    }
    
    long http_code = 0;
    curl_easy_getinfo(pImpl_->curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (http_code >= 400) {
        std::filesystem::remove(temp_path);
        return std::unexpected(HttpErrorInfo{
            HttpError::HttpStatusError,
            std::format("HTTP error: {}", http_code),
            static_cast<int>(http_code)
        });
    }
    
    // Move temporary file to final location
    std::error_code ec;
    std::filesystem::rename(temp_path, output_path, ec);
    if (ec) {
        std::filesystem::remove(temp_path);
        return std::unexpected(HttpErrorInfo{
            HttpError::FileWriteError,
            std::format("Failed to move file to final location: {}", ec.message())
        });
    }
    
    return {};
}

} // namespace hfdown
