#pragma once

#include "socket_wrapper.hpp"
#include "http_client.hpp"
#include <string>
#include <map>
#include <expected>
#include <optional>

namespace hfdown {

struct HttpRequest {
    std::string method = "GET";
    std::string path = "/";
    std::string host;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status_code = 0;
    std::string status_message;
    std::map<std::string, std::string> headers;
    size_t content_length = 0;
    bool chunked = false;
};

class HttpProtocol {
public:
    static std::string build_request(const HttpRequest& req);
    static std::expected<HttpResponse, HttpErrorInfo> parse_response(Socket& socket);
    static std::expected<size_t, HttpErrorInfo> read_chunk(Socket& socket, std::span<char> buffer);
    static std::expected<void, HttpErrorInfo> skip_chunk_trailer(Socket& socket);
    
private:
    static std::optional<std::string> get_header(const HttpResponse& resp, const std::string& name);
    static bool header_equals(const std::string& value, const std::string& expected);
};

} // namespace hfdown
