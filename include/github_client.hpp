#pragma once

#include "http_client.hpp"
#include <string>
#include <filesystem>
#include <expected>
#include <vector>

namespace hfdown {

struct FileUpload {
    std::filesystem::path local_path;
    std::string repo_path;
    std::string message;
};

enum class GitHubError {
    AuthRequired,
    RepoNotFound,
    UploadFailed,
    InvalidPath,
    NetworkError
};

struct GitHubErrorInfo {
    GitHubError error;
    std::string message;
};

class GitHubClient {
public:
    GitHubClient();
    GitHubClient(std::string token, std::string owner, std::string repo);
    
    // Upload single file to repository
    std::expected<void, GitHubErrorInfo> upload_file(
        const std::filesystem::path& file_path,
        const std::string& repo_path,
        const std::string& message = "Upload file"
    );
    
    // Upload multiple files
    std::expected<void, GitHubErrorInfo> upload_files(
        const std::vector<FileUpload>& files
    );
    
    // Check if file exists in repo
    std::expected<bool, GitHubErrorInfo> file_exists(const std::string& repo_path);

private:
    std::string token_;
    std::string owner_;
    std::string repo_;
    std::string branch_ = "main";
    HttpClient http_client_;
    
    std::string get_api_url(const std::string& path) const;
    std::expected<std::string, GitHubErrorInfo> get_file_sha(const std::string& repo_path);
    std::string encode_base64(const std::vector<char>& data) const;
};

} // namespace hfdown
