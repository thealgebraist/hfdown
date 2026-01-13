#pragma once

#include "http_client.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <expected>

namespace hfdown {

struct KaggleFile {
    std::string name;
    size_t size;
    std::string url;
};

struct DatasetInfo {
    std::string owner;
    std::string dataset;
    std::vector<KaggleFile> files;
    size_t total_size;
};

enum class KaggleError {
    DatasetNotFound,
    NetworkError,
    ParseError,
    AuthRequired,
    InvalidDatasetId
};

struct KaggleErrorInfo {
    KaggleError error;
    std::string message;
};

class KaggleClient {
public:
    KaggleClient();
    KaggleClient(std::string username, std::string key);
    
    // Get dataset information
    std::expected<DatasetInfo, KaggleErrorInfo> get_dataset_info(const std::string& dataset_id);
    
    // Download a specific file from a dataset
    std::expected<void, KaggleErrorInfo> download_file(
        const std::string& dataset_id,
        const std::string& filename,
        const std::filesystem::path& output_path,
        ProgressCallback progress_callback = nullptr
    );
    
    // Download entire dataset
    std::expected<void, KaggleErrorInfo> download_dataset(
        const std::string& dataset_id,
        const std::filesystem::path& output_dir,
        ProgressCallback progress_callback = nullptr,
        size_t parallel_downloads = 4
    );

private:
    std::string username_;
    std::string key_;
    HttpClient http_client_;
    
    std::string get_api_url(const std::string& owner, const std::string& dataset) const;
    std::string get_download_url(const std::string& owner, const std::string& dataset, 
                                 const std::string& filename) const;
    std::pair<std::string, std::string> parse_dataset_id(const std::string& dataset_id) const;
};

} // namespace hfdown
