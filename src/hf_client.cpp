#include "hf_client.hpp"
#include "json.hpp"
#include "compact_log.hpp"
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>

namespace hfdown {

HuggingFaceClient::HuggingFaceClient() = default;

HuggingFaceClient::HuggingFaceClient(std::string token) 
    : token_(std::move(token)) {
    if (!token_.empty()) {
        std::string auth = "Bearer "; auth += token_;
        http_client_.set_header("Authorization", auth);
    }
}

void HuggingFaceClient::set_config(const HttpConfig& config) {
    config_ = config;
    http_client_.set_config(config);
}

std::string HuggingFaceClient::get_api_url(const std::string& model_id) const {
    std::string base = use_mirror_ ? mirror_url_ : "https://huggingface.co";
    std::string url = base; url += "/api/models/"; url += model_id; url += "/tree/main?recursive=true";
    return url;
}

std::string HuggingFaceClient::get_file_url(const std::string& model_id, 
                                            const std::string& filename) const {
    std::string base = use_mirror_ ? mirror_url_ : "https://huggingface.co";
    std::string url = base; url += "/"; url += model_id; url += "/resolve/main/"; url += filename;
    return url;
}

std::expected<ModelInfo, HFErrorInfo> HuggingFaceClient::get_model_info(
    const std::string& model_id
) {
    auto response = http_client_.get(get_api_url(model_id));
    
    if (!response) {
        const auto& err = response.error();
        if (err.status_code == 404) {
            return std::unexpected(HFErrorInfo{
                HFError::ModelNotFound,
                "Model not found"
            });
        }
        return std::unexpected(HFErrorInfo{
            HFError::NetworkError,
            err.message
        });
    }
    
    ModelInfo info;
    info.model_id = model_id;
    
    struct TempFile {
        std::string path;
        size_t size = 0;
        std::string oid;
        bool is_file = false;
    } current;

    json::SAXParser::parse_tree_api(response->body, [&](std::string_view k, std::string_view v, bool is_str) {
        if (k == "type") current.is_file = (v == "file");
        else if (k == "path") current.path = std::string(v);
        else if (k == "size" && !is_str) {
            std::from_chars(v.data(), v.data() + v.size(), current.size);
        }
        else if (k == "oid") current.oid = std::string(v);
        
        if (current.is_file && !current.path.empty() && current.size > 0) {
            info.files.push_back({current.path, current.size, current.oid});
            current = {};
        }
    });
    
    return info;
}

std::expected<void, HFErrorInfo> HuggingFaceClient::download_file(
    const std::string& model_id,
    const std::string& filename,
    const std::filesystem::path& output_path,
    ProgressCallback progress_callback
) {
    auto info = get_model_info(model_id);
    std::string expected_oid = "";
    if (info) {
        for (const auto& f : info->files) {
            if (f.filename == filename) {
                expected_oid = f.oid;
                break;
            }
        }
    }

    std::string url = get_file_url(model_id, filename);
    auto result = http_client_.download_file(url, output_path, progress_callback, 0, expected_oid);
    
    if (!result) {
        return std::unexpected(HFErrorInfo{
            HFError::NetworkError,
            "Download failed"
        });
    }
    return {};
}

std::expected<void, HFErrorInfo> HuggingFaceClient::download_model(
    const std::string& model_id,
    const std::filesystem::path& output_dir,
    ProgressCallback progress_callback,
    size_t /*parallel_downloads*/
) {
    auto model_info = get_model_info(model_id);
    if (!model_info) return std::unexpected(model_info.error());
    
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    
    std::vector<ModelFile> files_to_download;
    size_t total_bytes = 0;
    size_t total_downloaded_bytes = 0;

    for (const auto& file : model_info->files) {
        total_bytes += file.size;
        auto file_path = output_dir / file.filename;
        if (std::filesystem::exists(file_path) && std::filesystem::file_size(file_path) == file.size) {
            total_downloaded_bytes += file.size;
        } else {
            files_to_download.push_back(file);
        }
    }

    auto last_update = std::chrono::steady_clock::now();
    const size_t total_files = model_info->files.size();
    size_t file_count = total_files - files_to_download.size();

    auto last_heartbeat = std::chrono::steady_clock::now();

    for (const auto& file : files_to_download) {
        auto now_hb = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now_hb - last_heartbeat).count() >= 1) {
            compact::Writer::error("[HF] Heartbeat: "); compact::Writer::print_num(file_count);
            compact::Writer::error("/"); compact::Writer::print_num(total_files); compact::Writer::error("\n");
            last_heartbeat = now_hb;
        }

        auto file_path = output_dir / file.filename;
        if (file_path.has_parent_path()) std::filesystem::create_directories(file_path.parent_path());
        
        std::string url = get_file_url(model_id, file.filename);

        auto file_callback = [&](const DownloadProgress& p) {
            if (!progress_callback) return;
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
            if (elapsed_ms >= 100 || p.downloaded_bytes >= p.total_bytes) {
                DownloadProgress global_p = p;
                global_p.downloaded_bytes = total_downloaded_bytes + p.downloaded_bytes;
                global_p.total_bytes = total_bytes;
                if (elapsed_ms > 0) {
                    global_p.speed_mbps = (static_cast<double>(p.downloaded_bytes) / (1024.0 * 1024.0)) / (static_cast<double>(elapsed_ms) / 1000.0);
                }
                progress_callback(global_p);
                last_update = now;
            }
        };

        auto result = http_client_.download_file(url, file_path, file_callback, 0, file.oid);
        if (!result) return std::unexpected(HFErrorInfo{HFError::NetworkError, "File failed"});
        total_downloaded_bytes += file.size;
        file_count++;
        compact::Writer::error("[HF] Completed "); compact::Writer::print_num(file_count);
        compact::Writer::error("/"); compact::Writer::print_num(total_files); 
        compact::Writer::error(": "); compact::Writer::error(file.filename); compact::Writer::error("\n");
    }
    
    compact::Writer::print("✓ Successfully downloaded model\n");
    return {};
}

} // namespace hfdown