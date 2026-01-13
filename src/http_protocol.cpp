#include "http_protocol.hpp"
#include "tls_socket.hpp"
#include <sstream>
#include <algorithm>
#include <format>
#include <cctype>

namespace hfdown {

std::string HttpProtocol::build_request(const HttpRequest& req) {
    std::ostringstream oss;
    oss << req.method << " " << req.path << " HTTP/1.1\r\n";
    oss << "Host: " << req.host << "\r\n";
    oss << "User-Agent: hfdown-cpp23/1.0\r\n";
    oss << "Accept: */*\r\n";
    oss << "Connection: close\r\n";
    
    for (const auto& [key, value] : req.headers) {
        oss << key << ": " << value << "\r\n";
    }
    
    oss << "\r\n";
    if (!req.body.empty()) oss << req.body;
    return oss.str();
}

std::optional<std::string> HttpProtocol::get_header(const HttpResponse& resp, const std::string& name) {
    auto lower_name = name;
    std::ranges::transform(lower_name, lower_name.begin(), ::tolower);
    
    for (const auto& [key, value] : resp.headers) {
        auto lower_key = key;
        std::ranges::transform(lower_key, lower_key.begin(), ::tolower);
        if (lower_key == lower_name) return value;
    }
    return std::nullopt;
}

bool HttpProtocol::header_equals(const std::string& value, const std::string& expected) {
    auto lower_value = value;
    std::ranges::transform(lower_value, lower_value.begin(), ::tolower);
    return lower_value == expected;
}

template<typename SocketType>
std::expected<HttpResponse, HttpErrorInfo> HttpProtocol::parse_response(SocketType& socket) {
    auto status_line = socket.read_until("\r\n");
    if (!status_line) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to read status line"});
    }
    
    HttpResponse response;
    std::istringstream iss(*status_line);
    std::string http_version;
    iss >> http_version >> response.status_code;
    std::getline(iss, response.status_message);
    
    while (true) {
        auto line = socket.read_until("\r\n");
        if (!line) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to read headers"});
        if (*line == "\r\n") break;
        
        if (auto colon = line->find(':'); colon != std::string::npos) {
            auto key = line->substr(0, colon);
            auto value = line->substr(colon + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            response.headers[key] = value;
        }
    }
    
    if (auto cl = get_header(response, "content-length")) {
        response.content_length = std::stoull(*cl);
    }
    
    if (auto te = get_header(response, "transfer-encoding")) {
        response.chunked = header_equals(*te, "chunked");
    }
    
    return response;
}

template<typename SocketType>
std::expected<size_t, HttpErrorInfo> HttpProtocol::read_chunk(SocketType& socket, std::span<char> buffer) {
    auto size_line = socket.read_until("\r\n");
    if (!size_line) {
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to read chunk size"});
    }
    
    size_t chunk_size = 0;
    std::istringstream iss(*size_line);
    iss >> std::hex >> chunk_size;
    
    if (chunk_size == 0) return 0;
    
    size_t to_read = std::min(chunk_size, buffer.size());
    size_t total_read = 0;
    
    while (total_read < to_read) {
        auto result = socket.read(buffer.subspan(total_read, to_read - total_read));
        if (!result) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to read chunk data"});
        if (*result == 0) break;
        total_read += *result;
    }
    
    socket.read_until("\r\n");
    return total_read;
}

template<typename SocketType>
std::expected<void, HttpErrorInfo> HttpProtocol::skip_chunk_trailer(SocketType& socket) {
    while (true) {
        auto line = socket.read_until("\r\n");
        if (!line) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to read trailer"});
        if (*line == "\r\n") break;
    }
    return {};
}

// Explicit template instantiations
template std::expected<HttpResponse, HttpErrorInfo> HttpProtocol::parse_response(Socket&);
template std::expected<HttpResponse, HttpErrorInfo> HttpProtocol::parse_response(TlsSocket&);
template std::expected<size_t, HttpErrorInfo> HttpProtocol::read_chunk(Socket&, std::span<char>);
template std::expected<size_t, HttpErrorInfo> HttpProtocol::read_chunk(TlsSocket&, std::span<char>);
template std::expected<void, HttpErrorInfo> HttpProtocol::skip_chunk_trailer(Socket&);
template std::expected<void, HttpErrorInfo> HttpProtocol::skip_chunk_trailer(TlsSocket&);

} // namespace hfdown
