#include "hf_client.hpp"
#include "json.hpp"
#include <format>
#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
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
                        
                        // Get OID if available (prefer LFS OID for SHA256 verification)
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
    // We need OID for checksum verification, so we must get model info first
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
    
    // 1. Sort files by size DESCENDING (Predictive Scheduling)
    // This ensures large files start early and small files fill the gaps.
    std::vector<ModelFile> sorted_files = model_info->files;
    std::sort(sorted_files.begin(), sorted_files.end(), [](const ModelFile& a, const ModelFile& b) {
        return a.size > b.size;
    });

    // 2. Identify files that need downloading
    std::vector<ModelFile> files_to_download;
    std::vector<ModelFile> already_downloaded;
    size_t total_expected_bytes = 0;

    for (const auto& file : sorted_files) {
        auto file_path = output_dir / file.filename;
        // Check if already exists with correct size. 
        // In a real app we'd also check hash/OID here if available.
        if (std::filesystem::exists(file_path) && std::filesystem::file_size(file_path) == file.size) {
            already_downloaded.push_back(file);
        } else {
            files_to_download.push_back(file);
            total_expected_bytes += file.size;
        }
    }

    if (total_expected_bytes > 0) {
        std::error_code ec;
        auto space_info = std::filesystem::space(output_dir.has_parent_path() ? output_dir.parent_path() : ".", ec);
        if (!ec && space_info.available < total_expected_bytes) {
            return std::unexpected(HFErrorInfo{
                HFError::NetworkError,
                std::format("Insufficient disk space. Need {:.2f} GB, have {:.2f} GB available.", 
                            total_expected_bytes / (1024.0*1024*1024), 
                            space_info.available / (1024.0*1024*1024))
            });
        }
    }

    // 3. Pre-allocate disk space for files to download
    for (const auto& file : files_to_download) {
        auto file_path = output_dir / file.filename;
        if (file_path.has_parent_path()) {
            std::filesystem::create_directories(file_path.parent_path());
        }
        
        // Pre-allocate space
        int fd = open(file_path.c_str(), O_WRONLY | O_CREAT, 0644);
        if (fd != -1) {
#ifdef __APPLE__
            fstore_t store;
            memset(&store, 0, sizeof(store));
            store.fst_flags = F_ALLOCATEALL;
            store.fst_posmode = 0; 
            store.fst_offset = 0;
            store.fst_length = (off_t)file.size;
            fcntl(fd, F_PREALLOCATE, &store);
            ftruncate(fd, file.size);
#else
            posix_fallocate(fd, 0, file.size);
#endif
            close(fd);
        }
    }

    // 4. Pre-calculate total size for weighted progress
    size_t total_bytes = 0;
    for (const auto& file : sorted_files) {
        total_bytes += file.size;
    }

    std::atomic<size_t> total_downloaded_bytes{0};
    for (const auto& file : already_downloaded) {
        total_downloaded_bytes.fetch_add(file.size);
    }

    std::mutex progress_mutex;
    auto last_global_update = std::chrono::steady_clock::now();

    const size_t total_files = sorted_files.size();
    std::atomic<size_t> completed_files{already_downloaded.size()};
    std::atomic<bool> has_error{false};
    std::mutex error_mutex;
    HFErrorInfo first_error{HFError::NetworkError, ""};
    
    // 5. Create Download Tasks
    struct DownloadTask {
        ModelFile file;
        size_t range_start = 0;
        size_t range_end = 0; // 0 means rest of file
        size_t chunk_size = 0;
    };

    std::queue<DownloadTask> task_queue;
    for (const auto& file : files_to_download) {
        // Split files > 250MB into 100MB chunks for parallel processing
        const size_t CHUNK_THRESHOLD = 250 * 1024 * 1024;
        const size_t CHUNK_SIZE = 100 * 1024 * 1024;

        if (file.size > CHUNK_THRESHOLD) {
            for (size_t start = 0; start < file.size; start += CHUNK_SIZE) {
                size_t end = std::min(start + CHUNK_SIZE - 1, file.size - 1);
                DownloadTask task{file, start, end, end - start + 1};
                task_queue.push(task);
            }
        } else {
            task_queue.push({file, 0, 0, file.size});
        }
    }

    std::mutex queue_mutex;
    
    // Worker function
    auto worker = [&]() {
        while (true) {
            DownloadTask task;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (task_queue.empty() || has_error.load()) {
                    return;
                }
                task = task_queue.front();
                task_queue.pop();
            }
            
            auto file_path = output_dir / task.file.filename;
            
            // Adjust config based on total file size
            HttpConfig config;
            if (task.file.size > 100 * 1024 * 1024) {
                config.buffer_size = 1024 * 1024;
                config.file_buffer_size = 4 * 1024 * 1024;
            }

            // Global Weighted Progress Wrapper
            size_t chunk_downloaded_so_far = 0;
            auto weighted_callback = [&](const DownloadProgress& p) {
                if (!progress_callback) return;
                size_t delta = p.downloaded_bytes - chunk_downloaded_so_far;
                chunk_downloaded_so_far = p.downloaded_bytes;
                size_t current_total = total_downloaded_bytes.fetch_add(delta) + delta;

                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(progress_mutex);
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_global_update).count();
                if (elapsed >= 250 || current_total >= total_bytes) {
                    DownloadProgress global_p;
                    global_p.downloaded_bytes = current_total;
                    global_p.total_bytes = total_bytes;
                    global_p.speed_mbps = p.speed_mbps; 
                    progress_callback(global_p);
                    last_global_update = now;
                }
            };

            // Set Range header for the chunk
            Http3Client thread_http_client;
            thread_http_client.set_config(config);
            if (task.range_end > 0) {
                thread_http_client.set_header("Range", std::format("bytes={}-{}", task.range_start, task.range_end));
            }

            if (!token_.empty()) {
                thread_http_client.set_header("Authorization", std::format("Bearer {}", token_));
            }
            
            std::string url = get_file_url(model_id, task.file.filename);
            // Pass task.range_start as the write_offset
            auto result = thread_http_client.download_file(url, file_path, weighted_callback, 0, "", task.range_start);
            
            if (!result) {
                bool expected = false;
                if (has_error.compare_exchange_strong(expected, true)) {
                    std::lock_guard<std::mutex> lock(error_mutex);
                    first_error = HFErrorInfo{HFError::NetworkError, result.error().message};
                }
                return;
            }
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
