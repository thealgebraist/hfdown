#include "cache_manager.hpp"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <openssl/evp.h>

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

std::expected<std::string, CacheErrorInfo> CacheManager::compute_hash(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected(CacheErrorInfo{CacheError::IOError, "Failed to open file"});
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return std::unexpected(CacheErrorInfo{CacheError::IOError, "Failed to create context"});
    
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::unexpected(CacheErrorInfo{CacheError::IOError, "Failed to init"});
    }
    
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }
    
    unsigned char hash_bytes[32];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, hash_bytes, &len);
    EVP_MD_CTX_free(ctx);
    
    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash_bytes[i];
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
                ce.size = entry["size"].is_number() ? (size_t)entry["size"].as_number() : 0;
                ce.ref_count = entry["refs"].is_number() ? (size_t)entry["refs"].as_number() : 1;
                cache_index_[name] = ce;
            }
        }
    } catch (...) {}
}

void CacheManager::save_index() {

    std::ostringstream oss;

    oss << "{\"entries\":{";

    bool first = true;

    for (const auto& [name, entry] : cache_index_) {

        if (!first) oss << ",";

        first = false;

        oss << "\"" << name << "\":{\"hash\":\"" << entry.hash 

            << "\",\"path\":\"" << entry.path.string() 

            << "\",\"size\":" << entry.size 

            << ",\"refs\":" << entry.ref_count << "}";

    }

    oss << "}}";

    

    std::ofstream ofs(cache_dir_ / "index.json");

    ofs << oss.str();

}



} // namespace hfdown
