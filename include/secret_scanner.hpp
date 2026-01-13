#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <regex>

namespace hfdown {

struct SecretPattern {
    std::string name;
    std::regex pattern;
    std::string description;
};

class SecretScanner {
public:
    SecretScanner();
    
    // Check if file contains potential secrets
    bool has_secrets(const std::filesystem::path& file_path) const;
    
    // Get list of detected secrets with line numbers
    std::vector<std::string> find_secrets(const std::filesystem::path& file_path) const;
    
    // Check if file type should be scanned (skip binary images/audio)
    bool should_scan(const std::filesystem::path& file_path) const;
    
    // Install pre-commit hook to prevent secret commits
    static bool install_hook(const std::filesystem::path& repo_path);
    
private:
    std::vector<SecretPattern> patterns_;
    std::vector<std::string> safe_extensions_;
    
    void init_patterns();
    void init_safe_extensions();
};

} // namespace hfdown
