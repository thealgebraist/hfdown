#include "http_client.hpp"
#include <curl/curl.h>
#include <fstream>
#include <chrono>
#include <cstring>
#include <format>

namespace hfdown {

// CURL callback for writing data to file
static size_t write_file_callback(void* ptr, size_t size, size_t nmemb, void* stream) {
    auto* file = static_cast<std::ofstream*>(stream);
    size_t written = size * nmemb;
    file->write(static_cast<const char*>(ptr), written);
    return file->good() ? written : 0;
}

// CURL callback for writing data to string
static size_t write_string_callback(void* ptr, size_t size, size_t nmemb, void* stream) {
    auto* str = static_cast<std::string*>(stream);
    size_t written = size * nmemb;
    str->append(static_cast<const char*>(ptr), written);
    return written;
}

// CURL progress callback
struct ProgressData {
    ProgressCallback callback;
    std::chrono::steady_clock::time_point start_time;
    size_t last_downloaded = 0;
    std::chrono::steady_clock::time_point last_time;
};

static int progress_callback_func(void* clientp, curl_off_t dltotal, curl_off_t dlnow, 
                            curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    if (!clientp) return 0;
    
    auto* data = static_cast<ProgressData*>(clientp);
    if (!data->callback) return 0;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - data->last_time).count();
    
    // Update every 100ms
    if (elapsed < 100 && dlnow < dltotal) return 0;
    
    DownloadProgress progress;
    progress.downloaded_bytes = static_cast<size_t>(dlnow);
    progress.total_bytes = static_cast<size_t>(dltotal);
    
    // Calculate speed in MB/s
    if (elapsed > 0) {
        double bytes_diff = dlnow - data->last_downloaded;
        double time_diff = elapsed / 1000.0; // Convert to seconds
        progress.speed_mbps = (bytes_diff / (1024.0 * 1024.0)) / time_diff;
    }
    
    data->callback(progress);
    data->last_downloaded = dlnow;
    data->last_time = now;
    
    return 0;
}

class HttpClient::Impl {
public:
    CURL* curl = nullptr;
    curl_slist* headers = nullptr;
    long timeout = 300; // 5 minutes default
    
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
        : curl(other.curl), headers(other.headers), timeout(other.timeout) {
        other.curl = nullptr;
        other.headers = nullptr;
    }
    
    Impl& operator=(Impl&& other) noexcept {
        if (this != &other) {
            if (headers) curl_slist_free_all(headers);
            if (curl) curl_easy_cleanup(curl);
            
            curl = other.curl;
            headers = other.headers;
            timeout = other.timeout;
            
            other.curl = nullptr;
            other.headers = nullptr;
        }
        return *this;
    }
};

HttpClient::HttpClient() : pImpl_(std::make_unique<Impl>()) {}

HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

void HttpClient::set_header(const std::string& key, const std::string& value) {
    std::string header = std::format("{}: {}", key, value);
    pImpl_->headers = curl_slist_append(pImpl_->headers, header.c_str());
}

void HttpClient::set_timeout(long seconds) {
    pImpl_->timeout = seconds;
}

std::expected<std::string, HttpErrorInfo> HttpClient::get(const std::string& url) {
    if (!pImpl_->curl) {
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
    
    if (http_code >= 400) {
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
    const std::filesystem::path& output_path,
    ProgressCallback progress_callback
) {
    if (!pImpl_->curl) {
        return std::unexpected(HttpErrorInfo{
            HttpError::NetworkError,
            "CURL not initialized"
        });
    }
    
    // Create parent directories if they don't exist
    if (output_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(output_path.parent_path(), ec);
        if (ec) {
            return std::unexpected(HttpErrorInfo{
                HttpError::FileWriteError,
                std::format("Failed to create directories: {}", ec.message())
            });
        }
    }
    
    // Download to temporary file first
    auto temp_path = output_path;
    temp_path += ".incomplete";
    
    std::ofstream file(temp_path, std::ios::binary);
    if (!file) {
        return std::unexpected(HttpErrorInfo{
            HttpError::FileWriteError,
            std::format("Failed to open file for writing: {}", temp_path.string())
        });
    }
    
    ProgressData progress_data;
    progress_data.callback = progress_callback;
    progress_data.start_time = std::chrono::steady_clock::now();
    progress_data.last_time = progress_data.start_time;
    
    curl_easy_setopt(pImpl_->curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(pImpl_->curl, CURLOPT_WRITEFUNCTION, write_file_callback);
    curl_easy_setopt(pImpl_->curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(pImpl_->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(pImpl_->curl, CURLOPT_TIMEOUT, pImpl_->timeout);
    curl_easy_setopt(pImpl_->curl, CURLOPT_USERAGENT, "hfdown/1.0");
    
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
