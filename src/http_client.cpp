#include "http_client.hpp"
#include "tls_socket.hpp"
#include "async_file_writer.hpp"
#include <iostream>
#include <fstream>
#include <format>
#include <sstream>
#include <charconv>
#include <algorithm>
#include <map>
#include <thread>
#include <chrono>

namespace hfdown {

// URL Parser helper
struct ParsedUrl {
    std::string host;
    uint16_t port;
    std::string path;
    std::string protocol;
};

static ParsedUrl parse_url(std::string_view url) {
    ParsedUrl result;
    result.port = 443;
    result.protocol = "https";
    result.path = "/";

    std::string_view remaining = url;
    if (remaining.starts_with("http://")) {
        result.protocol = "http";
        result.port = 80;
        remaining.remove_prefix(7);
    } else if (remaining.starts_with("https://")) {
        remaining.remove_prefix(8);
    }

    auto path_pos = remaining.find('/');
    if (path_pos != std::string_view::npos) {
        result.host = std::string(remaining.substr(0, path_pos));
        result.path = std::string(remaining.substr(path_pos));
    } else {
        result.host = std::string(remaining);
    }

    auto col_pos = result.host.find(':');
    if (col_pos != std::string::npos) {
        auto port_str = result.host.substr(col_pos + 1);
        result.host = result.host.substr(0, col_pos);
        std::from_chars(port_str.data(), port_str.data() + port_str.size(), result.port);
    }

    return result;
}

class HttpClient::Impl {
public:
    std::map<std::string, std::string> headers;
    long timeout = 300;
    HttpConfig config;

    Impl() {}
    
    std::expected<HttpResponse, HttpErrorInfo> perform_request(
        std::string_view method, 
        std::string_view url_view, 
        std::string_view body = "",
        std::function<bool(const char*, size_t)> body_callback = nullptr
    ) {
        std::string current_url(url_view);
        int redirects = 0;
        
        while (redirects < 5) {
            auto url = parse_url(current_url);
            if (url.protocol != "https") {
                return std::unexpected(HttpErrorInfo{HttpError::ProtocolError, "Only HTTPS supported"});
            }

            TlsSocket socket;
            socket.set_timeout(timeout);
            if (auto res = socket.connect(url.host, url.port); !res) {
                return std::unexpected(HttpErrorInfo{HttpError::ConnectionFailed, res.error().message});
            }

            std::stringstream req;
            req << method << " " << url.path << " HTTP/1.1\r\n";
            req << "Host: " << url.host << "\r\n";
            req << "User-Agent: hfdown/1.0\r\n";
            req << "Connection: close\r\n"; // Keep-alive simplified out for now
            
            for (const auto& [k, v] : headers) {
                req << k << ": " << v << "\r\n";
            }
            if (!body.empty()) {
                req << "Content-Length: " << body.size() << "\r\n";
                req << "Content-Type: application/json\r\n"; // Default for POST
            }
            req << "\r\n";
            req << body;

            std::string req_str = req.str();
            if (auto res = socket.write(req_str); !res) {
                return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to send request"});
            }

            // Read Status Line
            std::string status_line;
            if (auto res = socket.read_until("\r\n")) {
                status_line = *res;
            } else {
                return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to read status"});
            }

            HttpResponse response;
            response.protocol = "http/1.1";
            if (status_line.starts_with("HTTP/1.1 ") || status_line.starts_with("HTTP/1.0 ")) {
                if (status_line.length() >= 12) {
                    std::from_chars(status_line.data() + 9, status_line.data() + 12, response.status_code);
                }
            }

            // Read Headers
            long content_length = -1;
            bool chunked = false;
            std::string location;

            while (true) {
                auto header_res = socket.read_until("\r\n");
                if (!header_res) break;
                std::string header = *header_res;
                if (header == "\r\n") break; // End of headers
                
                // Trim CRLF
                if (header.ends_with("\r\n")) header.resize(header.size() - 2);
                
                auto colon = header.find(':');
                if (colon != std::string::npos) {
                    std::string key = header.substr(0, colon);
                    std::string val = header.substr(colon + 1);
                    // Trim val
                    val.erase(0, val.find_first_not_of(" "));
                    
                    std::string key_lower = key;
                    std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
                    
                    response.headers[key] = val;
                    
                    if (key_lower == "content-length") {
                        std::from_chars(val.data(), val.data() + val.size(), content_length);
                    } else if (key_lower == "transfer-encoding" && val.find("chunked") != std::string::npos) {
                        chunked = true;
                    } else if (key_lower == "location") {
                        location = val;
                    } else if (key_lower == "alt-svc") {
                        response.alt_svc = val;
                    }
                }
            }

            if (response.status_code >= 300 && response.status_code < 400 && !location.empty()) {
                if (location.starts_with("/")) {
                   // Relative redirect (simplified)
                   current_url = "https://" + url.host + location;
                } else {
                   current_url = location;
                }
                redirects++;
                continue;
            }

            // Read Body
            if (body_callback) {
                // Stream to callback
                std::vector<char> buf(config.buffer_size); 
                size_t total_read = 0;
                
                // Simple content-length reading
                if (content_length >= 0) {
                    size_t remaining = content_length;
                    while (remaining > 0) {
                        size_t to_read = std::min(remaining, buf.size());
                        auto read_res = socket.read({buf.data(), to_read});
                        if (!read_res || *read_res == 0) break;
                        if (!body_callback(buf.data(), *read_res)) return std::unexpected(HttpErrorInfo{HttpError::FileWriteError, "Write aborted"});
                        remaining -= *read_res;
                        total_read += *read_res;
                    }
                } else if (!chunked) {
                    // Read until close
                     while (true) {
                        auto read_res = socket.read({buf.data(), buf.size()});
                        if (!read_res || *read_res == 0) break;
                         if (!body_callback(buf.data(), *read_res)) return std::unexpected(HttpErrorInfo{HttpError::FileWriteError, "Write aborted"});
                         total_read += *read_res;
                     }
                } else {
                     // Chunked encoding not implemented for minimal example, assume mostly safetensors use C-L
                     return std::unexpected(HttpErrorInfo{HttpError::ProtocolError, "Chunked encoding not supported in minimal client"});
                }
            } else {
                 // String body
                 // Reuse logic basically
                 std::string body_str;
                 if (content_length > 0) body_str.reserve(content_length);
                 
                 std::vector<char> buf(16384);
                  if (content_length >= 0) {
                    size_t remaining = content_length;
                    while (remaining > 0) {
                        size_t to_read = std::min(remaining, buf.size());
                        auto read_res = socket.read({buf.data(), to_read});
                        if (!read_res || *read_res == 0) break;
                        body_str.append(buf.data(), *read_res);
                        remaining -= *read_res;
                    }
                } else {
                     while (true) {
                        auto read_res = socket.read({buf.data(), buf.size()});
                        if (!read_res || *read_res == 0) break;
                        body_str.append(buf.data(), *read_res);
                     }
                }
                response.body = std::move(body_str);
            }

            return response;
        }
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Too many redirects"});
    }
};

HttpClient::HttpClient() : pImpl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;
HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

void HttpClient::set_header(std::string_view key, std::string_view value) {
    pImpl_->headers[std::string(key)] = std::string(value);
}

void HttpClient::set_timeout(long seconds) { pImpl_->timeout = seconds; }
void HttpClient::set_config(const HttpConfig& config) { pImpl_->config = config; }

std::expected<HttpResponse, HttpErrorInfo> HttpClient::get_full(std::string_view url) {
    return pImpl_->perform_request("GET", url);
}

std::expected<std::string, HttpErrorInfo> HttpClient::get(std::string_view url) {
    auto res = get_full(url);
    if (!res) return std::unexpected(res.error());
    if (res->status_code >= 400) return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP Error {}", res->status_code), res->status_code});
    return std::move(res->body);
}

std::expected<std::string, HttpErrorInfo> HttpClient::post(std::string_view url, std::string_view body) {
    auto res = pImpl_->perform_request("POST", url, body);
    if (!res) return std::unexpected(res.error());
    if (res->status_code >= 400) return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP Error {}", res->status_code), res->status_code});
    return std::move(res->body);
}

std::expected<void, HttpErrorInfo> HttpClient::download_file(
    std::string_view url_sv, 
    const std::filesystem::path& output_path, 
    ProgressCallback progress_callback, 
    size_t resume_offset, 
    std::string_view expected_checksum, 
    size_t write_offset
) {
    if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path());
    AsyncFileWriter writer(output_path, 0); // Size 0 initially, will append
    
    // If resume_offset > 0, add Range header
    if (resume_offset > 0) {
        pImpl_->headers["Range"] = std::format("bytes={}-", resume_offset + write_offset);
    }

    size_t current_downloaded = 0;
    auto last_time = std::chrono::steady_clock::now();
    size_t last_downloaded_snapshot = 0;

    auto result = pImpl_->perform_request("GET", url_sv, "", [&](const char* data, size_t len) -> bool {
        // Write to file
        auto w_res = writer.write_at(data, len, write_offset + resume_offset + current_downloaded);
        if (!w_res) return false;
        
        current_downloaded += len;
        
        // Progress
        if (progress_callback) {
             auto now = std::chrono::steady_clock::now();
             auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
             if (ms >= pImpl_->config.progress_update_ms) {
                 DownloadProgress cv;
                 cv.downloaded_bytes = resume_offset + current_downloaded;
                 // Note: total bytes is hard to know here inside the callback without context, 
                 // but simple client can ignore it or we capture from header content-length.
                 
                 double speed = ((double)(current_downloaded - last_downloaded_snapshot) / (1024*1024)) / (ms / 1000.0);
                 cv.speed_mbps = speed;
                 
                 progress_callback(cv);
                 last_time = now;
                 last_downloaded_snapshot = current_downloaded;
             }
        }
        return true;
    });

    if (resume_offset > 0) pImpl_->headers.erase("Range"); // Cleanup
    writer.close();

    if (!result) return std::unexpected(result.error());
    if (result->status_code >= 400) return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP Error {}", result->status_code), result->status_code});
    
    return {};
}

} // namespace hfdown