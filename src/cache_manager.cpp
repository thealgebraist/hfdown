#include "cache_manager.hpp"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>

namespace hfdown {

CacheManager::CacheManager(const std::filesystem::path& cache_dir)
    : cache_dir_(cache_dir), index_file_(cache_dir / "index.json") {
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
    load_index();
}

std::filesystem::path CacheManager::get_cache_path(const std::string& hash) const {
    return cache_dir_ / "objects" / hash.substr(0, 2) / hash.substr(2);
}

std::expected<std::string, CacheErrorInfo> CacheManager::compute_hash(
    const std::filesystem::path& file_path
) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        return std::unexpected(CacheErrorInfo{CacheError::IOError, "Failed to open file"});
    }
    
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    
    std::array<char, 8192> buffer;
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
        SHA256_Update(&ctx, buffer.data(), file.gcount());
    }
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);
    
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return oss.str();
}

void CacheManager::load_index() {
    if (!std::filesystem::exists(index_file_)) return;
    
    std::ifstream file(index_file_);
    if (!file) return;
    
    try {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto data = json::parse(content);
        
        if (data["entries"].is_object()) {
            for (const auto& [name, entry] : data["entries"].as_object()) {
                CacheEntry ce;
                ce.hash = entry["hash"].is_string() ? entry["hash"].as_string() : "";
                ce.path = entry["path"].is_string() ? entry["path"].as_string() : "";
                ce.size = entry["size"].is_number() ? static_cast<size_t>(entry["size"].as_number()) : 0;
                ce.ref_count = entry["refs"].is_number() ? static_cast<size_t>(entry["refs"].as_number()) : 1;
                cache_index_[name] = ce;
            }
        }
    } catch (...) {}
}

void CacheManager::save_index() {
    std::ostringstream oss;
    oss << "{\\\"entries\\\":{";
    
    bool first = true;
    for (const auto& [name, entry] : cache_index_) {
        if (!first) oss << ",";
        first = false;
        oss << "\\\"" << name << "\\\":{"
            << "\\\"hash\\\":\\\"" << entry.hash << "\\\","
            << "\\\"path\\\":\\\"" << entry.path.string() << "\\\","
            << "\\\"size\\\":" << entry.size << ","
            << "\\\"refs\\\":" << entry.ref_count << "}";
    }
    
    oss << "}}";
    
    std::ofstream file(index_file_);
    if (file) {
        file << oss.str();
    }
}

std::expected<std::filesystem::path, CacheErrorInfo> CacheManager::add_file(
    const std::filesystem::path& source_path,
    const std::string& logical_name
) {
    auto hash = compute_hash(source_path);
    if (!hash) return std::unexpected(hash.error());
    
    auto cache_path = get_cache_path(*hash);
    std::error_code ec;
    
    if (!std::filesystem::exists(cache_path)) {
        std::filesystem::create_directories(cache_path.parent_path(), ec);
        if (ec) return std::unexpected(CacheErrorInfo{CacheError::IOError, ec.message()});
        
        std::filesystem::copy_file(source_path, cache_path, ec);
        if (ec) return std::unexpected(CacheErrorInfo{CacheError::IOError, ec.message()});
    }
    
    CacheEntry entry{*hash, cache_path, std::filesystem::file_size(source_path), 1};
    cache_index_[logical_name] = entry;
    save_index();
    
    return cache_path;
}

std::optional<std::filesystem::path> CacheManager::get_cached_file(
    const std::string& logical_name
) {
    auto it = cache_index_.find(logical_name);
    if (it == cache_index_.end()) return std::nullopt;
    
    if (std::filesystem::exists(it->second.path)) {
        return it->second.path;
    }
    
    return std::nullopt;
}

std::expected<bool, CacheErrorInfo> CacheManager::deduplicate(
    const std::filesystem::path& file_path,
    const std::filesystem::path& target_path
) {
    auto hash = compute_hash(file_path);
    if (!hash) return std::unexpected(hash.error());
    
    for (const auto& [name, entry] : cache_index_) {
        if (entry.hash == *hash && std::filesystem::exists(entry.path)) {
            std::error_code ec;
            std::filesystem::remove(file_path, ec);
            std::filesystem::create_symlink(entry.path, target_path, ec);
            if (ec) return std::unexpected(CacheErrorInfo{CacheError::LinkFailed, ec.message()});
            return true;
        }
    }
    
    return false;
}

CacheStats CacheManager::get_stats() const {
    CacheStats stats{};
    std::map<std::string, size_t> hash_counts;
    
    for (const auto& [name, entry] : cache_index_) {
        stats.total_files++;
        stats.total_size += entry.size;
        hash_counts[entry.hash]++;
    }
    
    for (const auto& [hash, count] : hash_counts) {
        if (count > 1) {
            stats.deduplicated_files += count - 1;
            auto it = std::find_if(cache_index_.begin(), cache_index_.end(), [&](const auto& p) { 
                return p.second.hash == hash; 
            });
            if (it != cache_index_.end()) {
                stats.space_saved += it->second.size * (count - 1);
            }
        }
        stats.hash_refs[hash] = count;
    }
    
    return stats;
}

size_t CacheManager::clean_unused() {
    size_t removed = 0;
    
    for (auto it = cache_index_.begin(); it != cache_index_.end();) {
        if (it->second.ref_count == 0 && std::filesystem::exists(it->second.path)) {
            std::error_code ec;
            std::filesystem::remove(it->second.path, ec);
            if (!ec) removed++;
            it = cache_index_.erase(it);
        } else {
            ++it;
        }
    }
    
    save_index();
    return removed;
}

} // namespace hfdown
