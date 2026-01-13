#pragma once

#include <string>
#include <functional>
#include <optional>
#include <expected>
#include <filesystem>

namespace hfdown {

struct DownloadProgress {
    size_t downloaded_bytes = 0;
    size_t total_bytes = 0;
    double speed_mbps = 0.0;
    
    double percentage() const {
        return total_bytes > 0 ? (100.0 * downloaded_bytes / total_bytes) : 0.0;
    }
};

using ProgressCallback = std::function<void(const DownloadProgress&)>;

enum class HttpError {
    NetworkError,
    InvalidUrl,
    FileWriteError,
    HttpStatusError,
    Timeout,
    ConnectionFailed,
    ProtocolError
};

struct HttpErrorInfo {
    HttpError error;
    std::string message;
    int status_code = 0;
};

struct HttpConfig {
    size_t buffer_size = 512 * 1024;        // 512KB default
    size_t file_buffer_size = 1024 * 1024;  // 1MB default
    int progress_update_ms = 250;            // 250ms default
    bool enable_http2 = true;
    bool enable_tcp_nodelay = true;
    bool enable_tcp_keepalive = true;
    bool enable_resume = true;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    // Delete copy operations
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    
    // Allow move operations
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;
    
    // Download file to disk with progress callback
    std::expected<void, HttpErrorInfo> download_file(
        const std::string& url,
        const std::filesystem::path& output_path,
        ProgressCallback progress_callback = nullptr,
        size_t resume_offset = 0
    );
    
    // GET request returning response body as string
    std::expected<std::string, HttpErrorInfo> get(const std::string& url);

    // GET request returning full response object
    std::expected<struct HttpResponse, HttpErrorInfo> get_full(const std::string& url);
    
    // Set custom headers
    void set_header(const std::string& key, const std::string& value);
    
    // Set timeout in seconds
    void set_timeout(long seconds);
    
    // Set performance configuration
    void set_config(const HttpConfig& config);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace hfdown
