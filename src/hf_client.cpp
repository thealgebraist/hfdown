#include "hf_client.hpp"
#include "json.hpp"
#include <format>
#include <iostream>
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
        http_client_.set_header("Authorization", std::format("Bearer {}", token_));
    }
}

std::string HuggingFaceClient::get_api_url(const std::string& model_id) const {
    std::string base = use_mirror_ ? mirror_url_ : "https://huggingface.co";
    return std::format("{}/api/models/{}/tree/main?recursive=true", base, model_id);
}

std::string HuggingFaceClient::get_file_url(const std::string& model_id, 
                                            const std::string& filename) const {
    std::string base = use_mirror_ ? mirror_url_ : "https://huggingface.co";
    return std::format("{}/{}/resolve/main/{}", base, model_id, filename);
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
                std::format("Model '{}' not found", model_id)
            });
        }
        return std::unexpected(HFErrorInfo{
            HFError::NetworkError,
            err.message
        });
    }
    
    try {
        auto json_data = json::parse(response->body);
        
        ModelInfo info;
        info.model_id = model_id;
        
        if (json_data.is_array()) {
            for (const auto& item : json_data.as_array()) {
                if (item["type"].is_string() && item["type"].as_string() == "file") {
                    if (item["path"].is_string()) {
                        ModelFile file;
                        file.filename = item["path"].as_string();
                        file.size = item["size"].is_number() ? static_cast<size_t>(item["size"].as_number()) : 0;
                        
                        if (item["lfs"].is_object() && item["lfs"]["oid"].is_string()) {
                            file.oid = item["lfs"]["oid"].as_string();
                        }
                        info.files.push_back(file);
                    }
                }
            }
        }
        return info;
    } catch (const std::exception& e) {
        return std::unexpected(HFErrorInfo{
            HFError::ParseError,
            std::format("Failed to parse model info: {}", e.what())
        });
    }
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
            std::format("Failed to download {}: {}", filename, result.error().message)
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

    for (const auto& file : files_to_download) {
        auto file_path = output_dir / file.filename;
        if (file_path.has_parent_path()) std::filesystem::create_directories(file_path.parent_path());
        
        std::string url = get_file_url(model_id, file.filename);

        auto file_callback = [&](const DownloadProgress& p) {
            if (!progress_callback) return;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
            
            if (elapsed >= 500 || p.downloaded_bytes >= p.total_bytes) {
                DownloadProgress global_p = p;
                global_p.downloaded_bytes = total_downloaded_bytes + p.downloaded_bytes;
                global_p.total_bytes = total_bytes;
                progress_callback(global_p);
                last_update = now;
            }
        };

        auto result = http_client_.download_file(url, file_path, file_callback, 0, file.oid);
        if (!result) {
            return std::unexpected(HFErrorInfo{HFError::NetworkError, result.error().message});
        }
        total_downloaded_bytes += file.size;
    }
    
    std::cout << std::format("✓ Successfully downloaded {} files to \n", 
                            total_files, output_dir.string());
    
    return {};
}

} // namespace hfdown