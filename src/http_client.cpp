#include "http_client.hpp"
#include "socket_wrapper.hpp"
#include "tls_socket.hpp"
#include "http_protocol.hpp"
#include <fstream>
#include <chrono>
#include <format>
#include <algorithm>
#include <variant>

namespace hfdown {

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
        parts.port = (parts.protocol == "https") ? 443 : 80;
    }
    
    auto path_start = url_view.find('/');
    auto host_part = (path_start != std::string::npos) ? url_view.substr(0, path_start) : url_view;
    parts.path = (path_start != std::string::npos) ? url_view.substr(path_start) : "/";
    
    if (auto port_pos = host_part.find(':'); port_pos != std::string::npos) {
        parts.host = host_part.substr(0, port_pos);
        parts.port = std::stoi(host_part.substr(port_pos + 1));
    } else {
        parts.host = host_part;
    }
    
    return parts;
}

class HttpClient::Impl {
public:
    std::map<std::string, std::string> headers;
    long timeout = 300;
    HttpConfig config;
};

HttpClient::HttpClient() : pImpl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;
HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

void HttpClient::set_header(const std::string& key, const std::string& value) {
    pImpl_->headers[key] = value;
}

void HttpClient::set_timeout(long seconds) {
    pImpl_->timeout = seconds;
}

void HttpClient::set_config(const HttpConfig& config) {
    pImpl_->config = config;
}

template<typename SocketType>
std::expected<std::string, HttpErrorInfo> get_impl(SocketType& socket, const UrlParts& parts, 
    const std::map<std::string, std::string>& headers) {
    HttpRequest request{.method = "GET", .path = parts.path, .host = parts.host, .headers = headers};
    auto request_str = HttpProtocol::build_request(request);
    
    if (auto write_res = socket.write(std::span{request_str.data(), request_str.size()}); !write_res) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to send request"});
    }
    
    auto response = HttpProtocol::parse_response(socket);
    if (!response) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, response.error().message});
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
    return body;
}

std::expected<std::string, HttpErrorInfo> HttpClient::get(const std::string& url) {
    auto parts = parse_url(url);
    
    if (parts.protocol == "https") {
        TlsSocket socket;
        socket.set_timeout(pImpl_->timeout);
        if (auto conn = socket.connect(parts.host, parts.port); !conn) {
            return std::unexpected(HttpErrorInfo{HttpError::NetworkError, conn.error().message});
        }
        return get_impl(socket, parts, pImpl_->headers);
    } else {
        Socket socket;
        socket.set_timeout(pImpl_->timeout);
        if (auto conn = socket.connect(parts.host, parts.port); !conn) {
            return std::unexpected(HttpErrorInfo{HttpError::NetworkError, conn.error().message});
        }
        return get_impl(socket, parts, pImpl_->headers);
    }
}

template<typename SocketType>
std::expected<void, HttpErrorInfo> download_impl(SocketType& socket, const UrlParts& parts,
    const std::filesystem::path& output_path, const std::map<std::string, std::string>& headers,
    ProgressCallback progress_callback, const HttpConfig& config) {
    auto temp_path = std::filesystem::path(output_path).concat(".incomplete");
    std::ofstream file(temp_path, std::ios::binary);
    std::vector<char> file_buffer(config.file_buffer_size);
    file.rdbuf()->pubsetbuf(file_buffer.data(), config.file_buffer_size);
    
    HttpRequest request{.method = "GET", .path = parts.path, .host = parts.host, .headers = headers};
    auto request_str = HttpProtocol::build_request(request);
    if (auto write_res = socket.write(std::span{request_str.data(), request_str.size()}); !write_res) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to send request"});
    }
    
    auto response = HttpProtocol::parse_response(socket);
    if (!response) { std::filesystem::remove(temp_path); return std::unexpected(HttpErrorInfo{HttpError::NetworkError, response.error().message}); }
    if (response->status_code >= 400) { std::filesystem::remove(temp_path); return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP {}", response->status_code), response->status_code}); }
    
    auto last_time = std::chrono::steady_clock::now();
    size_t downloaded = 0, last_downloaded = 0;
    std::vector<char> buffer(config.buffer_size);
    
    auto update_progress = [&]() {
        if (!progress_callback) return;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (elapsed < config.progress_update_ms) return;
        DownloadProgress progress{.downloaded_bytes = downloaded, .total_bytes = response->content_length};
        if (elapsed > 0) progress.speed_mbps = ((downloaded - last_downloaded) / (1024.0 * 1024.0)) / (elapsed / 1000.0);
        progress_callback(progress);
        last_downloaded = downloaded;
        last_time = now;
    };
    
    if (response->chunked) {
        while (auto chunk = HttpProtocol::read_chunk(socket, std::span{buffer})) {
            if (*chunk == 0) break;
            file.write(buffer.data(), *chunk);
            downloaded += *chunk;
            update_progress();
        }
    } else {
        while (auto bytes = socket.read(std::span{buffer})) {
            if (*bytes == 0) break;
            file.write(buffer.data(), *bytes);
            downloaded += *bytes;
            update_progress();
        }
    }
    
    file.close();
    std::error_code ec;
    std::filesystem::rename(temp_path, output_path, ec);
    if (ec) { std::filesystem::remove(temp_path); return std::unexpected(HttpErrorInfo{HttpError::FileWriteError, std::format("Failed to move file: {}", ec.message())}); }
    return {};
}

std::expected<void, HttpErrorInfo> HttpClient::download_file(
    const std::string& url,
    const std::filesystem::path& output_path,
    ProgressCallback progress_callback
) {
    auto parts = parse_url(url);
    
    if (output_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(output_path.parent_path(), ec);
        if (ec) return std::unexpected(HttpErrorInfo{HttpError::FileWriteError, std::format("Failed to create directories: {}", ec.message())});
    }
    
    if (parts.protocol == "https") {
        TlsSocket socket;
        socket.set_timeout(pImpl_->timeout);
        if (auto conn = socket.connect(parts.host, parts.port); !conn) {
            return std::unexpected(HttpErrorInfo{HttpError::NetworkError, conn.error().message});
        }
        return download_impl(socket, parts, output_path, pImpl_->headers, progress_callback, pImpl_->config);
    } else {
        Socket socket;
        socket.set_timeout(pImpl_->timeout);
        if (auto conn = socket.connect(parts.host, parts.port); !conn) {
            return std::unexpected(HttpErrorInfo{HttpError::NetworkError, conn.error().message});
        }
        return download_impl(socket, parts, output_path, pImpl_->headers, progress_callback, pImpl_->config);
    }
}

} // namespace hfdown
