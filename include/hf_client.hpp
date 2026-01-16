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

enum class DownloadState {
    Idle,
    Fetching_Model_Info,
    Planning_Downloads,
    Downloading_Chunks,
    Finalizing_Download,
    Download_Complete,
    Error_State
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

    void set_config(const HttpConfig& config);

    void use_mirror(bool enable) { use_mirror_ = enable; }
    void set_mirror_url(const std::string& url) { mirror_url_ = url; }

    std::string get_file_url(const std::string& model_id, const std::string& filename) const;
    Http3Client http_client_;

private:
    std::string token_;
    HttpClient http1_client_; // For non-H3 downloads
    HttpConfig config_;
    bool use_mirror_ = false;
    std::string mirror_url_ = "https://hf-mirror.com";
    DownloadState current_state_ = DownloadState::Idle;
    
    std::string get_api_url(const std::string& model_id) const;
};

} // namespace hfdown