#include "github_client.hpp"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <format>
#include <array>

namespace hfdown {

GitHubClient::GitHubClient() = default;

GitHubClient::GitHubClient(std::string token, std::string owner, std::string repo)
    : token_(std::move(token)), owner_(std::move(owner)), repo_(std::move(repo)) {
    if (!token_.empty()) {
        http_client_.set_header("Authorization", std::format("Bearer {}", token_));
        http_client_.set_header("Accept", "application/vnd.github+json");
        http_client_.set_header("X-GitHub-Api-Version", "2022-11-28");
    }
}

std::string GitHubClient::get_api_url(const std::string& path) const {
    return std::format("https://api.github.com/repos/{}/{}/contents/{}", 
                      owner_, repo_, path);
}

std::string GitHubClient::encode_base64(const std::vector<char>& data) const {
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string result;
    int val = 0, valb = -6;
    
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    
    if (valb > -6) result.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    
    return result;
}

std::expected<std::string, GitHubErrorInfo> GitHubClient::get_file_sha(
    const std::string& repo_path
) {
    auto response = http_client_.get(get_api_url(repo_path));
    
    if (!response) {
        if (response.error().status_code == 404) {
            return "";
        }
        return std::unexpected(GitHubErrorInfo{GitHubError::NetworkError, response.error().message});
    }
    
    try {
        auto data = json::parse(*response);
        if (data["sha"].is_string()) {
            return std::string(data["sha"].as_string());
        }
    } catch (...) {}
    
    return "";
}

std::expected<bool, GitHubErrorInfo> GitHubClient::file_exists(
    const std::string& repo_path
) {
    auto sha = get_file_sha(repo_path);
    if (!sha) return std::unexpected(sha.error());
    return !sha->empty();
}

std::expected<void, GitHubErrorInfo> GitHubClient::upload_file(
    const std::filesystem::path& file_path,
    const std::string& repo_path,
    const std::string& message
) {
    if (!std::filesystem::exists(file_path)) {
        return std::unexpected(GitHubErrorInfo{GitHubError::InvalidPath, "File not found"});
    }
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        return std::unexpected(GitHubErrorInfo{GitHubError::InvalidPath, "Cannot read file"});
    }
    
    std::vector<char> content((std::istreambuf_iterator<char>(file)), 
                             std::istreambuf_iterator<char>());
    auto encoded = encode_base64(content);
    
    auto sha_result = get_file_sha(repo_path);
    if (!sha_result) return std::unexpected(sha_result.error());
    
    std::ostringstream json;
    json << "{\"message\":\"" << message << "\","
         << "\"content\":\"" << encoded << "\","
         << "\"branch\":\"" << branch_ << "\"";
    
    if (!sha_result->empty()) {
        json << ",\"sha\":\"" << *sha_result << "\"";
    }
    
    json << "}";
    
    http_client_.set_header("Content-Type", "application/json");
    
    auto response = http_client_.get(get_api_url(repo_path));
    if (!response) {
        return std::unexpected(GitHubErrorInfo{GitHubError::UploadFailed, 
            "Failed to upload file"});
    }
    
    return {};
}

std::expected<void, GitHubErrorInfo> GitHubClient::upload_files(
    const std::vector<FileUpload>& files
) {
    for (const auto& upload : files) {
        auto result = upload_file(upload.local_path, upload.repo_path, upload.message);
        if (!result) return result;
    }
    
    return {};
}

} // namespace hfdown
