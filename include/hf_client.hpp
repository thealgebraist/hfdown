#pragma once

#include "http3_client.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <expected>

namespace hfdown {

struct ModelFile {
    std::string filename;
    size_t size;
    std::string oid; // Git LFS object ID
};

struct ModelInfo {
    std::string model_id;
    std::vector<ModelFile> files;
};

enum class HFError {
    ModelNotFound,
    NetworkError,
    ParseError,
    InvalidModelId,
    AuthRequired
};

struct HFErrorInfo {
    HFError error;
    std::string message;
};

class HuggingFaceClient {
public:
    HuggingFaceClient();
    explicit HuggingFaceClient(std::string token);
    
    // Get model information
    std::expected<ModelInfo, HFErrorInfo> get_model_info(const std::string& model_id);
    
    // Download a specific file from a model
    std::expected<void, HFErrorInfo> download_file(
        const std::string& model_id,
        const std::string& filename,
        const std::filesystem::path& output_path,
        ProgressCallback progress_callback = nullptr
    );
    
    // Download entire model
    std::expected<void, HFErrorInfo> download_model(
        const std::string& model_id,
        const std::filesystem::path& output_dir,
        ProgressCallback progress_callback = nullptr,
        size_t parallel_downloads = 4
    );

    void set_protocol(const std::string& protocol) {
        http_client_.set_protocol(protocol);
    }

    void use_mirror(bool enable) { use_mirror_ = enable; }
    void set_mirror_url(const std::string& url) { mirror_url_ = url; }

private:
    std::string token_;
    Http3Client http_client_;
    bool use_mirror_ = false;
    std::string mirror_url_ = "https://hf-mirror.com";
    
    std::string get_api_url(const std::string& model_id) const;
    std::string get_file_url(const std::string& model_id, const std::string& filename) const;
};

} // namespace hfdown