#include "http_client.hpp"
#include "http_protocol.hpp"
#include "async_file_writer.hpp"
#include <curl/curl.h>
#include <fstream>
#include <chrono>
#include <format>
#include <iostream>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <charconv>

namespace hfdown {

struct ProgressData {
    ProgressCallback callback;
    std::chrono::steady_clock::time_point last_time;
    size_t last_downloaded = 0;
};

struct DownloadContext {
    AsyncFileWriter* writer;
    size_t current_offset;
    EVP_MD_CTX* sha_ctx;
    bool use_checksum;
};

static size_t write_string_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* body = static_cast<std::string*>(userp);
    if (body->size() + realsize > body->max_size()) return 0;
    body->append(static_cast<char*>(contents), realsize);
    return realsize;
}

static size_t write_file_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* ctx = static_cast<DownloadContext*>(userp);
    auto result = ctx->writer->write_at(contents, realsize, ctx->current_offset);
    if (result) {
        ctx->current_offset += realsize;
        if (ctx->use_checksum) EVP_DigestUpdate(ctx->sha_ctx, contents, realsize);
    } else {
        return 0;
    }
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
        value.erase(value.find_last_not_of(" 	\r\n") + 1);
        response->headers[key] = value;
        if (key == "Alt-Svc" || key == "alt-svc") response->alt_svc = value;
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
        if (elapsed > 0) progress.speed_mbps = ((dlnow - data->last_downloaded) / (1024.0 * 1024.0)) / (elapsed / 1000.0);
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
    Impl() { curl_global_init(CURL_GLOBAL_ALL); }
    ~Impl() { curl_global_cleanup(); }
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

static void set_http_version(CURL* curl, const HttpConfig& config, std::string_view url) {
    if (config.enable_http2) {
        if (url.starts_with("https://")) {
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
        } else {
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        }
    }
}

std::expected<HttpResponse, HttpErrorInfo> HttpClient::get_full(std::string_view url_sv) {
    std::string url(url_sv);
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to init CURL"});
    HttpResponse response;
    std::string body;
    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : pImpl_->headers) {
        std::string h = k; h += ": "; h += v;
        chunk = curl_slist_append(chunk, h.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, pImpl_->timeout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)pImpl_->config.buffer_size);
    set_http_version(curl, pImpl_->config, url);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(chunk);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, curl_easy_strerror(res)});
    }
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response.status_code = static_cast<int>(http_code);
    response.body = std::move(body);
    curl_easy_cleanup(curl);
    return response;
}

std::expected<std::string, HttpErrorInfo> HttpClient::get(std::string_view url) {
    auto res = get_full(url);
    if (!res) return std::unexpected(res.error());
    if (res->status_code >= 400) return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP Error {}", res->status_code), res->status_code});
    return std::move(res->body);
}

std::expected<std::string, HttpErrorInfo> HttpClient::post(std::string_view url_sv, std::string_view body_sv) {
    std::string url(url_sv);
    std::string body(body_sv);
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to init CURL"});
    std::string response_body;
    HttpResponse response;
    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : pImpl_->headers) {
        std::string h = k; h += ": "; h += v;
        chunk = curl_slist_append(chunk, h.c_str());
    }
    chunk = curl_slist_append(chunk, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    set_http_version(curl, pImpl_->config, url);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(chunk);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        return std::unexpected(HttpErrorInfo{HttpError::NetworkError, curl_easy_strerror(res), (int)http_code});
    }
    
    curl_easy_cleanup(curl);
    if (http_code >= 400) return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP Error {}", (int)http_code), (int)http_code});
    return response_body;
}

std::expected<void, HttpErrorInfo> HttpClient::download_file(std::string_view url_sv, const std::filesystem::path& output_path, ProgressCallback progress_callback, size_t resume_offset, std::string_view expected_checksum, size_t write_offset) {
    std::string url(url_sv);
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected(HttpErrorInfo{HttpError::NetworkError, "Failed to init CURL"});
    if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path());
    AsyncFileWriter writer(output_path, 0);
    DownloadContext dctx{&writer, write_offset + resume_offset, nullptr, !expected_checksum.empty()};
    if (dctx.use_checksum) {
        dctx.sha_ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(dctx.sha_ctx, EVP_sha256(), nullptr);
        if (resume_offset > 0 || write_offset > 0) { dctx.use_checksum = false; EVP_MD_CTX_free(dctx.sha_ctx); dctx.sha_ctx = nullptr; }
    }
    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : pImpl_->headers) {
        std::string h = k; h += ": "; h += v;
        chunk = curl_slist_append(chunk, h.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dctx);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)pImpl_->config.buffer_size);
    set_http_version(curl, pImpl_->config, url);
    ProgressData pdata{progress_callback, std::chrono::steady_clock::now(), 0};
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback_func);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pdata);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (resume_offset > 0) curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)resume_offset);
    CURLcode res = curl_easy_perform(curl);
    writer.close(); curl_slist_free_all(chunk);
    if (res != CURLE_OK) {
        if (dctx.sha_ctx) EVP_MD_CTX_free(dctx.sha_ctx);
        curl_easy_cleanup(curl); return std::unexpected(HttpErrorInfo{HttpError::NetworkError, curl_easy_strerror(res)});
    }
    long http_code = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if ((resume_offset > 0 || write_offset > 0) && http_code == 200) {
        if (dctx.sha_ctx) EVP_MD_CTX_free(dctx.sha_ctx);
        curl_easy_cleanup(curl); return std::unexpected(HttpErrorInfo{HttpError::ProtocolError, "Expected 206"});
    }
    if (http_code >= 400 && http_code != 206) {
        if (dctx.sha_ctx) EVP_MD_CTX_free(dctx.sha_ctx);
        curl_easy_cleanup(curl); return std::unexpected(HttpErrorInfo{HttpError::HttpStatusError, std::format("HTTP Error {}", (int)http_code), (int)http_code});
    }
    if (dctx.use_checksum && dctx.sha_ctx) {
        unsigned char hash[32]; unsigned int hash_len = 0;
        EVP_DigestFinal_ex(dctx.sha_ctx, hash, &hash_len);
        EVP_MD_CTX_free(dctx.sha_ctx);
        std::string actual; actual.reserve(64);
        for (unsigned int i = 0; i < hash_len; ++i) {
            actual += std::format("{:02x}", hash[i]);
        }
        if (actual != expected_checksum) { curl_easy_cleanup(curl); return std::unexpected(HttpErrorInfo{HttpError::ProtocolError, "Checksum mismatch"}); }
    } else if (dctx.sha_ctx) EVP_MD_CTX_free(dctx.sha_ctx);
    curl_easy_cleanup(curl);
    return {};
}

} // namespace hfdown