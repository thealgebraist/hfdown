#include "hf_client.hpp"
#include "json.hpp"
#include "compact_log.hpp"
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <thread>
#include <mutex>
#include <queue>
#include <format>

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
        else if (k == "lfs" && !is_str) {
            json::SAXParser::parse_tree_api(v, [&](std::string_view lk, std::string_view lv, bool /*lis_str*/) {
                if (lk == "oid") current.oid = std::string(lv);
            });
        }
    }, [&]() {
        if (current.is_file && !current.path.empty() && current.size > 0) {
            info.files.push_back({current.path, current.size, current.oid});
        }
        current = {};
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
    auto result = http_client_.download_file(url, output_path, progress_callback, 0, expected_oid.size() == 64 ? expected_oid : "");
    
    if (!result) {
        return std::unexpected(HFErrorInfo{
            HFError::NetworkError,
            "Download failed: " + result.error().message
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

    const size_t num_threads = 4;
    const size_t fixed_buffer_size = 32 * 1024 * 1024; // 32MB
    
    std::queue<ModelFile> queue;
    for (const auto& f : files_to_download) queue.push(f);
    
    std::mutex mtx;
    std::vector<std::thread> workers;
    std::atomic<size_t> global_downloaded{total_downloaded_bytes};
    std::atomic<size_t> file_count{model_info->files.size() - files_to_download.size()};
    std::atomic<bool> failed{false};
    std::string first_error;
    std::vector<std::string> active_filenames(num_threads, "Idle");

    auto start_time = std::chrono::steady_clock::now();

    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back([&, i]() {
            Http3Client thread_client;
            if (!token_.empty()) {
                thread_client.set_header("Authorization", "Bearer " + token_);
            }
            HttpConfig thread_config = config_;
            thread_config.buffer_size = fixed_buffer_size;
            thread_client.set_config(thread_config);

            while (true) {
                ModelFile file;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    if (queue.empty() || failed) {
                        active_filenames[i] = "Done";
                        break;
                    }
                    file = queue.front();
                    queue.pop();
                    active_filenames[i] = file.filename;
                }

                auto file_path = output_dir / file.filename;
                if (file_path.has_parent_path()) {
                    std::lock_guard<std::mutex> lock(mtx);
                    std::filesystem::create_directories(file_path.parent_path());
                }

                std::string url = get_file_url(model_id, file.filename);
                size_t last_file_downloaded = 0;

                auto file_callback = [&](const DownloadProgress& p) {
                    size_t diff = p.downloaded_bytes - last_file_downloaded;
                    last_file_downloaded = p.downloaded_bytes;
                    global_downloaded += diff;

                    if (progress_callback) {
                        auto now = std::chrono::steady_clock::now();
                        static std::atomic<uint64_t> last_msg_ms{0};
                        static std::atomic<size_t> last_global_bytes{total_downloaded_bytes};
                        
                        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                        auto prev_ms = last_msg_ms.load();
                        
                        if (now_ms - prev_ms >= 100 || p.downloaded_bytes >= p.total_bytes) {
                            if (last_msg_ms.compare_exchange_strong(prev_ms, now_ms)) {
                                DownloadProgress global_p;
                                auto current_global = global_downloaded.load();
                                global_p.downloaded_bytes = current_global;
                                global_p.total_bytes = total_bytes;
                                
                                {
                                    std::lock_guard<std::mutex> lock(mtx);
                                    std::string active;
                                    for (const auto& name : active_filenames) {
                                        if (name != "Idle" && name != "Done") {
                                            if (!active.empty()) active += ", ";
                                            active += name;
                                        }
                                    }
                                    global_p.active_files = active;
                                }

                                auto delta_ms = now_ms - prev_ms;
                                if (delta_ms > 0) {
                                    size_t prev_bytes = last_global_bytes.exchange(current_global);
                                    size_t delta_bytes = (current_global > prev_bytes) ? (current_global - prev_bytes) : 0;
                                    global_p.speed_mbps = (static_cast<double>(delta_bytes) / (1024.0 * 1024.0)) / (static_cast<double>(delta_ms) / 1000.0);
                                }
                                progress_callback(global_p);
                            }
                        }
                    }
                };

                auto result = thread_client.download_file(url, file_path, file_callback, 0, file.oid.size() == 64 ? file.oid : "");
                if (!result) {
                    std::lock_guard<std::mutex> lock(mtx);
                    if (!failed) {
                        failed = true;
                        first_error = "File failed: " + file.filename + " - " + result.error().message;
                    }
                    return;
                }

                size_t current_file_count = ++file_count;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    compact::Writer::error("[HF] Completed "); 
                    compact::Writer::print_num(current_file_count);
                    compact::Writer::error("/"); 
                    compact::Writer::print_num(model_info->files.size()); 
                    compact::Writer::error(": "); 
                    compact::Writer::error(file.filename); 
                    compact::Writer::error("\n");
                }
            }
        });
    }

    for (auto& w : workers) w.join();

    if (failed) {
        return std::unexpected(HFErrorInfo{HFError::NetworkError, first_error});
    }
    
    compact::Writer::print("✓ Successfully downloaded model\n");
    return {};
}

} // namespace hfdown