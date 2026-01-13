#include "kaggle_client.hpp"
#include "json.hpp"
#include <format>
#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <sstream>

namespace hfdown {

KaggleClient::KaggleClient() = default;

KaggleClient::KaggleClient(std::string username, std::string key) 
    : username_(std::move(username)), key_(std::move(key)) {
    if (!username_.empty() && !key_.empty()) {
        std::string auth = std::format("{}:{}", username_, key_);
        std::string encoded = auth; // Base64 encoding would be done here in production
        http_client_.set_header("Authorization", std::format("Basic {}", encoded));
    }
}

std::pair<std::string, std::string> KaggleClient::parse_dataset_id(
    const std::string& dataset_id
) const {
    auto slash_pos = dataset_id.find('/');
    if (slash_pos == std::string::npos) {
        return {"", ""};
    }
    return {dataset_id.substr(0, slash_pos), dataset_id.substr(slash_pos + 1)};
}

std::string KaggleClient::get_api_url(const std::string& owner, 
                                      const std::string& dataset) const {
    return std::format("https://www.kaggle.com/api/v1/datasets/list/{}/{}", owner, dataset);
}

std::string KaggleClient::get_download_url(const std::string& owner, 
                                           const std::string& dataset,
                                           const std::string& filename) const {
    return std::format("https://www.kaggle.com/api/v1/datasets/download/{}/{}/{}", 
                      owner, dataset, filename);
}

std::expected<DatasetInfo, KaggleErrorInfo> KaggleClient::get_dataset_info(
    const std::string& dataset_id
) {
    auto [owner, dataset] = parse_dataset_id(dataset_id);
    
    if (owner.empty() || dataset.empty()) {
        return std::unexpected(KaggleErrorInfo{
            KaggleError::InvalidDatasetId,
            std::format("Invalid dataset ID format. Expected 'owner/dataset', got '{}'", dataset_id)
        });
    }
    
    auto response = http_client_.get(get_api_url(owner, dataset));
    
    if (!response) {
        const auto& err = response.error();
        if (err.status_code == 404) {
            return std::unexpected(KaggleErrorInfo{
                KaggleError::DatasetNotFound,
                std::format("Dataset '{}' not found", dataset_id)
            });
        }
        if (err.status_code == 401 || err.status_code == 403) {
            return std::unexpected(KaggleErrorInfo{
                KaggleError::AuthRequired,
                "Authentication required. Set KAGGLE_USERNAME and KAGGLE_KEY environment variables"
            });
        }
        return std::unexpected(KaggleErrorInfo{
            KaggleError::NetworkError,
            err.message
        });
    }
    
    try {
        auto json_data = json::parse(response->body);
        
        DatasetInfo info;
        info.owner = owner;
        info.dataset = dataset;
        info.total_size = 0;
        
        if (json_data["datasetFiles"].is_array()) {
            for (const auto& file : json_data["datasetFiles"].as_array()) {
                if (file["name"].is_string()) {
                    KaggleFile kf;
                    kf.name = file["name"].as_string();
                    
                    if (file["totalBytes"].is_number()) {
                        kf.size = static_cast<size_t>(file["totalBytes"].as_number());
                        info.total_size += kf.size;
                    } else {
                        kf.size = 0;
                    }
                    
                    kf.url = get_download_url(owner, dataset, kf.name);
                    info.files.push_back(kf);
                }
            }
        }
        
        return info;
        
    } catch (const std::exception& e) {
        return std::unexpected(KaggleErrorInfo{
            KaggleError::ParseError,
            std::format("Failed to parse dataset info: {}", e.what())
        });
    }
}

std::expected<void, KaggleErrorInfo> KaggleClient::download_file(
    const std::string& dataset_id,
    const std::string& filename,
    const std::filesystem::path& output_path,
    ProgressCallback progress_callback
) {
    auto [owner, dataset] = parse_dataset_id(dataset_id);
    
    if (owner.empty() || dataset.empty()) {
        return std::unexpected(KaggleErrorInfo{
            KaggleError::InvalidDatasetId,
            std::format("Invalid dataset ID format: {}", dataset_id)
        });
    }
    
    std::string url = get_download_url(owner, dataset, filename);
    
    auto result = http_client_.download_file(url, output_path, progress_callback);
    
    if (!result) {
        const auto& err = result.error();
        if (err.status_code == 404) {
            return std::unexpected(KaggleErrorInfo{
                KaggleError::DatasetNotFound,
                std::format("File '{}' not found in dataset '{}'", filename, dataset_id)
            });
        }
        if (err.status_code == 401 || err.status_code == 403) {
            return std::unexpected(KaggleErrorInfo{
                KaggleError::AuthRequired,
                "Authentication required"
            });
        }
        return std::unexpected(KaggleErrorInfo{
            KaggleError::NetworkError,
            err.message
        });
    }
    
    return {};
}

std::expected<void, KaggleErrorInfo> KaggleClient::download_dataset(
    const std::string& dataset_id,
    const std::filesystem::path& output_dir,
    ProgressCallback progress_callback,
    size_t parallel_downloads
) {
    auto info = get_dataset_info(dataset_id);
    if (!info) {
        return std::unexpected(info.error());
    }
    
    if (info->files.empty()) {
        return std::unexpected(KaggleErrorInfo{
            KaggleError::DatasetNotFound,
            std::format("No files found in dataset '{}'", dataset_id)
        });
    }
    
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        return std::unexpected(KaggleErrorInfo{
            KaggleError::NetworkError,
            std::format("Failed to create directory: {}", ec.message())
        });
    }
    
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::queue<size_t> file_queue;
    std::atomic<size_t> completed_files{0};
    std::atomic<bool> has_error{false};
    std::string error_message;
    
    for (size_t i = 0; i < info->files.size(); ++i) {
        file_queue.push(i);
    }
    
    auto worker = [&]() {
        while (true) {
            size_t file_idx;
            {
                std::unique_lock lock(queue_mutex);
                cv.wait(lock, [&] { return !file_queue.empty() || has_error; });
                
                if (has_error || file_queue.empty()) return;
                
                file_idx = file_queue.front();
                file_queue.pop();
            }
            
            const auto& file = info->files[file_idx];
            auto output_path = output_dir / file.name;
            
            std::cout << std::format("[{}/{}] Downloading {}...\n", 
                                    completed_files + 1, info->files.size(), file.name);
            
            auto result = download_file(dataset_id, file.name, output_path, progress_callback);
            
            if (!result) {
                has_error = true;
                error_message = result.error().message;
                cv.notify_all();
                return;
            }
            
            completed_files++;
        }
    };
    
    std::vector<std::thread> threads;
    for (size_t i = 0; i < parallel_downloads; ++i) {
        threads.emplace_back(worker);
    }
    
    cv.notify_all();
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    if (has_error) {
        return std::unexpected(KaggleErrorInfo{
            KaggleError::NetworkError,
            error_message
        });
    }
    
    std::cout << std::format("✓ Successfully downloaded {} files to {}\n", 
                            info->files.size(), output_dir.string());
    
    return {};
}

} // namespace hfdown
