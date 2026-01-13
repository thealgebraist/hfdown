#include "http_client.hpp"
#include "http_protocol.hpp"
#include <curl/curl.h>
#include <fstream>
#include <chrono>
#include <format>
#include <iostream>

namespace hfdown {

struct ProgressData {
    ProgressCallback callback;
    std::chrono::steady_clock::time_point last_time;
    size_t last_downloaded = 0;
};

static size_t write_string_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* body = static_cast<std::string*>(userp);
    body->append(static_cast<char*>(contents), realsize);
    return realsize;
}

static size_t write_file_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* file = static_cast<std::ofstream*>(userp);
    file->write(static_cast<char*>(contents), realsize);
    return realsize;
}

static size_t header_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* response = static_cast<HttpResponse*>(userp);
    std::string line(static_cast<char*>(contents), realsize);
    
    if (auto colon = line.find(':'); colon != std::string::npos) {
        auto key = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(" 	"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        response->headers[key] = value;
        
        if (key == "Alt-Svc" || key == "alt-svc") {
            response->alt_svc = value;
        }
    }
    return realsize;
}

static int progress_callback_func(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    auto* data = static_cast<ProgressData*>(clientp);
    if (!data->callback) return 0;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - data->last_time).count();
    
    if (elapsed >= 250 || dlnow == dltotal) {
        DownloadProgress progress;
        progress.downloaded_bytes = static_cast<size_t>(dlnow);
        progress.total_bytes = static_cast<size_t>(dltotal);
        if (elapsed > 0) {
            progress.speed_mbps = ((dlnow - data->last_downloaded) / (1024.0 * 1024.0)) / (elapsed / 1000.0);
        }
        data->callback(progress);
        data->last_time = now;
        data->last_downloaded = dlnow;
    }
    return 0;
}

class HttpClient::Impl {
public:
    std::map<std::string, std::string> headers;
    long timeout = 300;
    HttpConfig config;
    
    Impl() {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    
    ~Impl() {
        curl_global_cleanup();
    }
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

std::expected<HttpResponse, HttpErrorInfo> HttpClient::get_full(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to init CURL"});
    
    HttpResponse response;
    std::string body;
    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : pImpl_->headers) {
        chunk = curl_slist_append(chunk, std::format("{}: {}", k, v).c_str());
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, pImpl_->timeout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    
    if (pImpl_->config.enable_http2) {
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    }
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, curl_easy_strerror(res)});
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response.status_code = static_cast<int>(http_code);
    response.body = std::move(body);
    response.protocol = "http/1.1"; // Default
    
    long http_version = 0;
    curl_easy_getinfo(curl, CURLINFO_HTTP_VERSION, &http_version);
    if (http_version == CURL_HTTP_VERSION_2_0) response.protocol = "h2";
    else if (http_version == CURL_HTTP_VERSION_3) response.protocol = "h3";
    else if (http_version == CURL_HTTP_VERSION_1_1) response.protocol = "http/1.1";
    else response.protocol = std::format("http/{}", http_version);
    
    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);
    
    return response;
}

std::expected<std::string, HttpErrorInfo> HttpClient::get(const std::string& url) {
    auto res = get_full(url);
    if (!res) return std::unexpected(res.error());
    if (res->status_code >= 400) {
        return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP {}", res->status_code), res->status_code});
    }
    return std::move(res->body);
}

std::expected<void, HttpErrorInfo> HttpClient::download_file(
    const std::string& url,
    const std::filesystem::path& output_path,
    ProgressCallback progress_callback,
    size_t resume_offset
) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to init CURL"});
    
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    
    std::ofstream file(output_path, std::ios::binary | (resume_offset > 0 ? std::ios::app : std::ios::trunc));
    if (!file) {
        curl_easy_cleanup(curl);
        return std::unexpected(HttpErrorInfo{HttpError::FileWriteError, "Failed to open output file"});
    }
    
    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : pImpl_->headers) {
        chunk = curl_slist_append(chunk, std::format("{}: {}", k, v).c_str());
    }
    
    ProgressData pdata{progress_callback, std::chrono::steady_clock::now(), resume_offset};
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, pImpl_->timeout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    if (progress_callback) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback_func);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pdata);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }
    
    if (resume_offset > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resume_offset));
    }
    
    if (pImpl_->config.enable_http2) {
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    }
    
    CURLcode res = curl_easy_perform(curl);
    file.close();
    curl_slist_free_all(chunk);
    
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, curl_easy_strerror(res)});
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400) {
        curl_easy_cleanup(curl);
        return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP {}", http_code), static_cast<int>(http_code)});
    }
    
    curl_easy_cleanup(curl);
    return {};
}

} // namespace hfdown