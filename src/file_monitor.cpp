#include "file_monitor.hpp"
#include <iostream>
#include <thread>
#include <algorithm>
#include <ranges>

namespace hfdown {

FileMonitor::FileMonitor(const std::filesystem::path& watch_dir)
    : watch_dir_(watch_dir) {
    if (!std::filesystem::exists(watch_dir_)) {
        std::filesystem::create_directories(watch_dir_);
    }
}

void FileMonitor::set_extensions(const std::vector<std::string>& extensions) {
    extensions_ = extensions;
    for (auto& ext : extensions_) {
        if (!ext.empty() && ext[0] != '.') {
            ext = "." + ext;
        }
    }
}

bool FileMonitor::should_monitor(const std::filesystem::path& path) const {
    if (extensions_.empty()) return true;
    
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return std::ranges::any_of(extensions_, [&](const auto& monitored_ext) {
        std::string lower_monitored = monitored_ext;
        std::transform(lower_monitored.begin(), lower_monitored.end(), 
                      lower_monitored.begin(), ::tolower);
        return ext == lower_monitored;
    });
}

void FileMonitor::scan_directory() {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(watch_dir_)) {
        if (entry.is_regular_file() && should_monitor(entry.path())) {
            file_times_[entry.path()] = entry.last_write_time();
        }
    }
}

void FileMonitor::check_changes(FileChangeCallback& callback) {
    std::map<std::filesystem::path, std::filesystem::file_time_type> current_files;
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(watch_dir_)) {
        if (entry.is_regular_file() && should_monitor(entry.path())) {
            current_files[entry.path()] = entry.last_write_time();
        }
    }
    
    for (const auto& [path, time] : current_files) {
        auto it = file_times_.find(path);
        if (it == file_times_.end()) {
            callback(FileChange{path, FileChangeType::Added, std::chrono::system_clock::now()});
        } else if (it->second != time) {
            callback(FileChange{path, FileChangeType::Modified, std::chrono::system_clock::now()});
        }
    }
    
    for (const auto& [path, time] : file_times_) {
        if (current_files.find(path) == current_files.end()) {
            callback(FileChange{path, FileChangeType::Deleted, std::chrono::system_clock::now()});
        }
    }
    
    file_times_ = std::move(current_files);
}

void FileMonitor::start(FileChangeCallback callback, int interval_ms) {
    running_ = true;
    scan_directory();
    
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        check_changes(callback);
    }
}

void FileMonitor::stop() {
    running_ = false;
}

} // namespace hfdown
