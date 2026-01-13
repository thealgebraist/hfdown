#include "git_uploader.hpp"
#include <cstdio>
#include <array>
#include <memory>
#include <format>

namespace hfdown {

GitUploader::GitUploader(std::filesystem::path repo_path)
    : repo_path_(std::move(repo_path)) {}

bool GitUploader::is_git_repo() const {
    auto git_dir = repo_path_ / ".git";
    return std::filesystem::exists(git_dir);
}

std::expected<std::string, GitErrorInfo> GitUploader::run_git_command(const std::string& args) {
    auto cmd = std::format("cd {} && git {}", repo_path_.string(), args);
    std::array<char, 128> buffer;
    std::string result;
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return std::unexpected(GitErrorInfo{GitError::CommandFailed, "Failed to execute git"});
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    int exit_code = pclose(pipe.release());
    if (exit_code != 0) return std::unexpected(GitErrorInfo{GitError::CommandFailed, result});
    
    return result;
}

std::expected<void, GitErrorInfo> GitUploader::add_and_push(
    const std::filesystem::path& file_path,
    const std::string& commit_message
) {
    if (!is_git_repo()) {
        return std::unexpected(GitErrorInfo{GitError::NotGitRepo, "Not a git repository"});
    }
    
    if (!std::filesystem::exists(repo_path_ / file_path)) {
        return std::unexpected(GitErrorInfo{GitError::FileNotFound, "File not found"});
    }
    
    auto relative = std::filesystem::relative(file_path, repo_path_);
    
    if (auto r = run_git_command(std::format("add {}", relative.string())); !r) return std::unexpected(r.error());
    if (auto r = run_git_command(std::format("commit -m \"{}\"", commit_message)); !r) return std::unexpected(r.error());
    if (auto r = run_git_command("push"); !r) return std::unexpected(r.error());
    
    return {};
}

std::expected<void, GitErrorInfo> GitUploader::add_files_and_push(
    const std::vector<std::filesystem::path>& files,
    const std::string& commit_message
) {
    if (!is_git_repo()) {
        return std::unexpected(GitErrorInfo{GitError::NotGitRepo, "Not a git repository"});
    }
    
    std::string add_cmd = "add";
    for (const auto& file : files) {
        auto relative = std::filesystem::relative(file, repo_path_);
        add_cmd += std::format(" {}", relative.string());
    }
    
    if (auto r = run_git_command(add_cmd); !r) return std::unexpected(r.error());
    if (auto r = run_git_command(std::format("commit -m \"{}\"", commit_message)); !r) return std::unexpected(r.error());
    if (auto r = run_git_command("push"); !r) return std::unexpected(r.error());
    
    return {};
}

} // namespace hfdown
