#include "hf_client.hpp"
#include "json.hpp"
#include <format>
#include <iostream>

namespace hfdown {

HuggingFaceClient::HuggingFaceClient() = default;

HuggingFaceClient::HuggingFaceClient(std::string token) 
    : token_(std::move(token)) {
    if (!token_.empty()) {
        http_client_.set_header("Authorization", std::format("Bearer {}", token_));
    }
}

std::string HuggingFaceClient::get_api_url(const std::string& model_id) const {
    return std::format("https://huggingface.co/api/models/{}/tree/main", model_id);
}

std::string HuggingFaceClient::get_file_url(const std::string& model_id, 
                                            const std::string& filename) const {
    return std::format("https://huggingface.co/{}/resolve/main/{}", model_id, filename);
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
        auto json_data = json::parse(*response);
        
        ModelInfo info;
        info.model_id = model_id;
        
        // Parse tree API response (array of files/directories)
        if (json_data.is_array()) {
            for (const auto& item : json_data.as_array()) {
                // Only process files, not directories
                if (item["type"].is_string() && item["type"].as_string() == "file") {
                    if (item["path"].is_string()) {
                        ModelFile file;
                        file.filename = item["path"].as_string();
                        
                        // Get file size
                        if (item["size"].is_number()) {
                            file.size = static_cast<size_t>(item["size"].as_number());
                        } else {
                            file.size = 0;
                        }
                        
                        // Get OID if available
                        if (item["oid"].is_string()) {
                            file.oid = item["oid"].as_string();
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
    std::string url = get_file_url(model_id, filename);
    
    auto result = http_client_.download_file(url, output_path, progress_callback);
    
    if (!result) {
        const auto& err = result.error();
        return std::unexpected(HFErrorInfo{
            HFError::NetworkError,
            std::format("Failed to download {}: {}", filename, err.message)
        });
    }
    
    return {};
}

std::expected<void, HFErrorInfo> HuggingFaceClient::download_model(
    const std::string& model_id,
    const std::filesystem::path& output_dir,
    ProgressCallback progress_callback
) {
    // First, get model info to know what files to download
    auto model_info = get_model_info(model_id);
    if (!model_info) {
        return std::unexpected(model_info.error());
    }
    
    // Create output directory
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        return std::unexpected(HFErrorInfo{
            HFError::NetworkError,
            std::format("Failed to create output directory: {}", ec.message())
        });
    }
    
    // Download each file
    size_t total_files = model_info->files.size();
    size_t current_file = 0;
    
    for (const auto& file : model_info->files) {
        ++current_file;
        std::cout << std::format("[{}/{}] Downloading {}...\n", 
                                current_file, total_files, file.filename);
        
        auto file_path = output_dir / file.filename;
        
        // Create subdirectories if needed
        if (file_path.has_parent_path()) {
            std::filesystem::create_directories(file_path.parent_path(), ec);
        }
        
        auto result = download_file(model_id, file.filename, file_path, progress_callback);
        
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    
    std::cout << std::format("✓ Successfully downloaded {} files to {}\n", 
                            total_files, output_dir.string());
    
    return {};
}

} // namespace hfdown
