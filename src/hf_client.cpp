#include "hf_client.hpp"
#include "json.hpp"
#include <format>
#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

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
        auto json_data = json::parse(response->body);
        
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
    ProgressCallback progress_callback,
    size_t parallel_downloads
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
    
    const size_t total_files = model_info->files.size();
    std::atomic<size_t> completed_files{0};
    std::atomic<bool> has_error{false};
    std::mutex error_mutex;
    HFErrorInfo first_error{HFError::NetworkError, ""};
    
    // Queue of files to download
    std::queue<ModelFile> file_queue;
    for (const auto& file : model_info->files) {
        file_queue.push(file);
    }
    std::mutex queue_mutex;
    
    // Worker function
    auto worker = [&]() {
        while (true) {
            ModelFile file;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (file_queue.empty() || has_error.load()) {
                    return;
                }
                file = file_queue.front();
                file_queue.pop();
            }
            
            auto file_path = output_dir / file.filename;
            
            // Check if file already exists with correct size
            if (std::filesystem::exists(file_path)) {
                std::error_code ec;
                auto existing_size = std::filesystem::file_size(file_path, ec);
                if (!ec && file.size > 0 && existing_size == file.size) {
                    size_t current = completed_files.fetch_add(1) + 1;
                    std::cout << std::format("[{}/{}] Skipping {} (already exists)\n", 
                                            current, total_files, file.filename);
                    continue;
                }
            }
            
            size_t current = completed_files.load() + 1;
            std::cout << std::format("[{}/{}] Downloading {}...\n", 
                                    current, total_files, file.filename);
            
            // Create subdirectories if needed
            if (file_path.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(file_path.parent_path(), ec);
            }
            
            // Create a new HTTP client for each thread
            Http3Client thread_http_client;
            if (!token_.empty()) {
                thread_http_client.set_header("Authorization", 
                    std::format("Bearer {}", token_));
            }
            
            std::string url = get_file_url(model_id, file.filename);
            auto result = thread_http_client.download_file(url, file_path, progress_callback);
            
            if (!result) {
                bool expected = false;
                if (has_error.compare_exchange_strong(expected, true)) {
                    std::lock_guard<std::mutex> lock(error_mutex);
                    first_error = HFErrorInfo{
                        HFError::NetworkError,
                        std::format("Failed to download {}: {}", 
                                  file.filename, result.error().message)
                    };
                }
                return;
            }
            
            completed_files.fetch_add(1);
        }
    };
    
    // Launch worker threads
    std::vector<std::jthread> workers;
    size_t num_threads = std::min(parallel_downloads, total_files);
    workers.reserve(num_threads);
    
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(worker);
    }
    
    // Wait for all threads to complete
    workers.clear();
    
    if (has_error.load()) {
        std::lock_guard<std::mutex> lock(error_mutex);
        return std::unexpected(first_error);
    }
    
    std::cout << std::format("✓ Successfully downloaded {} files to {}\n", 
                            total_files, output_dir.string());
    
    return {};
}

} // namespace hfdown
