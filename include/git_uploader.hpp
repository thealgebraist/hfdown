#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <expected>

namespace hfdown {

enum class GitError {
    NotGitRepo,
    NoRemote,
    CommandFailed,
    FileNotFound
};

struct GitErrorInfo {
    GitError error;
    std::string message;
};

class GitUploader {
public:
    explicit GitUploader(std::filesystem::path repo_path);
    
    std::expected<void, GitErrorInfo> add_and_push(
        const std::filesystem::path& file_path,
        const std::string& commit_message = "Add file"
    );
    
    std::expected<void, GitErrorInfo> add_files_and_push(
        const std::vector<std::filesystem::path>& files,
        const std::string& commit_message = "Add files"
    );
    
    bool is_git_repo() const;
    
private:
    std::filesystem::path repo_path_;
    
    std::expected<std::string, GitErrorInfo> run_git_command(const std::string& args);
};

} // namespace hfdown
