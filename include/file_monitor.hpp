#pragma once

#include <filesystem>
#include <functional>
#include <vector>
#include <chrono>
#include <map>

namespace hfdown {

enum class FileChangeType {
    Added,
    Modified,
    Deleted
};

struct FileChange {
    std::filesystem::path path;
    FileChangeType type;
    std::chrono::system_clock::time_point timestamp;
};

using FileChangeCallback = std::function<void(const FileChange&)>;

class FileMonitor {
public:
    explicit FileMonitor(const std::filesystem::path& watch_dir);
    
    // Set file extensions to monitor (e.g., {".png", ".jpg", ".wav"})
    void set_extensions(const std::vector<std::string>& extensions);
    
    // Start monitoring (blocking)
    void start(FileChangeCallback callback, int interval_ms = 1000);
    
    // Stop monitoring
    void stop();
    
    // Check if file matches monitored extensions
    bool should_monitor(const std::filesystem::path& path) const;

private:
    std::filesystem::path watch_dir_;
    std::vector<std::string> extensions_;
    std::map<std::filesystem::path, std::filesystem::file_time_type> file_times_;
    bool running_ = false;
    
    void scan_directory();
    void check_changes(FileChangeCallback& callback);
};

} // namespace hfdown
