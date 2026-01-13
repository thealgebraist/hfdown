#pragma once

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
    std::string body;  // For small responses (http3-test)
    std::string protocol; // "h3", "h2", "http/1.1"
    std::string alt_svc; // Alt-Svc header content
};

} // namespace hfdown