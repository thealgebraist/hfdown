#pragma once

#include <string>
#include <filesystem>
#include <vector>
#include <expected>
#include <map>
#include <optional>
#include <array>

namespace hfdown {

struct CacheEntry {
    std::string hash;
    std::filesystem::path path;
    size_t size;
    size_t ref_count;
};

struct CacheStats {
    size_t total_files;
    size_t total_size;
    size_t deduplicated_files;
    size_t space_saved;
    std::map<std::string, size_t> hash_refs;
};

enum class CacheError {
    HashFailed,
    LinkFailed,
    NotFound,
    IOError
};

struct CacheErrorInfo {
    CacheError error;
    std::string message;
};

class CacheManager {
public:
    explicit CacheManager(const std::filesystem::path& cache_dir = ".hfcache");
    
    // Add file to cache and return cached path
    std::expected<std::filesystem::path, CacheErrorInfo> add_file(
        const std::filesystem::path& source_path,
        const std::string& logical_name
    );
    
    // Get file from cache if exists
    std::optional<std::filesystem::path> get_cached_file(const std::string& logical_name);
    
    // Link to existing cache entry if hash matches
    std::expected<bool, CacheErrorInfo> deduplicate(
        const std::filesystem::path& file_path,
        const std::filesystem::path& target_path
    );
    
    // Get cache statistics
    CacheStats get_stats() const;
    
    // Clean unused cache entries
    size_t clean_unused();
    
    // Compute file hash (SHA256)
    static std::expected<std::string, CacheErrorInfo> compute_hash(
        const std::filesystem::path& file_path
    );

private:
    std::filesystem::path cache_dir_;
    std::filesystem::path index_file_;
    
    void load_index();
    void save_index();
    std::filesystem::path get_cache_path(const std::string& hash) const;
    
    std::map<std::string, CacheEntry> cache_index_;
};

} // namespace hfdown
