#include "secret_scanner.hpp"
#include <fstream>
#include <algorithm>
#include <format>

namespace hfdown {

SecretScanner::SecretScanner() {
    init_patterns();
    init_safe_extensions();
}

void SecretScanner::init_patterns() {
    patterns_ = {
        {"GitHub Token", std::regex(R"(ghp_[a-zA-Z0-9]{36})"), "GitHub personal access token"},
        {"AWS Key", std::regex(R"(AKIA[0-9A-Z]{16})"), "AWS access key"},
        {"OpenAI Key", std::regex(R"(sk-[a-zA-Z0-9]{48})"), "OpenAI API key"},
        {"Generic API Key", std::regex(R"(['\"]?api[_-]?key['\"]?\s*[:=]\s*['\"]?[a-zA-Z0-9]{16,}['\"]?)"), "Generic API key"},
        {"Bearer Token", std::regex(R"(Bearer\s+[a-zA-Z0-9\-._~+/]+=*)"), "Bearer authentication token"},
        {"Password", std::regex(R"(['\"]?password['\"]?\s*[:=]\s*['\"]?[^'\"]{8,}['\"]?)"), "Password in config"},
        {"Private Key", std::regex(R"(-----BEGIN\s+(?:RSA|DSA|EC|OPENSSH)\s+PRIVATE\s+KEY-----)"), "Private key"},
        {"JWT Token", std::regex(R"(eyJ[a-zA-Z0-9_-]*\.eyJ[a-zA-Z0-9_-]*\.[a-zA-Z0-9_-]*)"), "JWT token"},
        {"Slack Token", std::regex(R"(xox[baprs]-[0-9]{10,13}-[0-9]{10,13}-[a-zA-Z0-9]{24,32})"), "Slack token"},
        {"HuggingFace Token", std::regex(R"(hf_[a-zA-Z0-9]{30,})"), "HuggingFace token"},
    };
}

void SecretScanner::init_safe_extensions() {
    safe_extensions_ = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".ico",
        ".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma",
        ".mp4", ".avi", ".mov", ".mkv", ".webm", ".flv",
        ".pdf", ".zip", ".tar", ".gz", ".7z", ".rar",
        ".bin", ".dat", ".db", ".sqlite", ".pkl", ".npy", ".npz",
        ".safetensors", ".pt", ".pth", ".ckpt", ".h5", ".tflite"
    };
}

bool SecretScanner::should_scan(const std::filesystem::path& file_path) const {
    auto ext = file_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return std::find(safe_extensions_.begin(), safe_extensions_.end(), ext) == safe_extensions_.end();
}

bool SecretScanner::has_secrets(const std::filesystem::path& file_path) const {
    if (!should_scan(file_path)) return false;
    
    std::ifstream file(file_path);
    if (!file.is_open()) return false;
    
    std::string line;
    while (std::getline(file, line)) {
        for (const auto& pattern : patterns_) {
            if (std::regex_search(line, pattern.pattern)) {
                return true;
            }
        }
    }
    
    return false;
}

std::vector<std::string> SecretScanner::find_secrets(const std::filesystem::path& file_path) const {
    std::vector<std::string> findings;
    
    if (!should_scan(file_path)) {
        return findings;
    }
    
    std::ifstream file(file_path);
    if (!file.is_open()) {
        findings.push_back(std::format("Error: Cannot open file {}", file_path.string()));
        return findings;
    }
    
    std::string line;
    int line_num = 1;
    
    while (std::getline(file, line)) {
        for (const auto& pattern : patterns_) {
            if (std::regex_search(line, pattern.pattern)) {
                findings.push_back(std::format("Line {}: {} detected", line_num, pattern.name));
            }
        }
        ++line_num;
    }
    
    return findings;
}

bool SecretScanner::install_hook(const std::filesystem::path& repo_path) {
    auto hooks_dir = repo_path / ".git" / "hooks";
    auto hook_path = hooks_dir / "pre-commit";
    
    if (!std::filesystem::exists(hooks_dir)) {
        std::filesystem::create_directories(hooks_dir);
    }
    
    std::ofstream hook(hook_path);
    if (!hook.is_open()) return false;
    
    hook << "#!/bin/bash\n";
    hook << "# Auto-generated secret scanner hook\n";
    hook << "FILES=$(git diff --cached --name-only --diff-filter=ACM)\n";
    hook << "for FILE in $FILES; do\n";
    hook << "  if grep -qE '(ghp_[a-zA-Z0-9]{36}|AKIA[0-9A-Z]{16}|sk-[a-zA-Z0-9]{48}|Bearer [a-zA-Z0-9])' \"$FILE\" 2>/dev/null; then\n";
    hook << "    echo \"⚠️  Secret detected in $FILE - commit blocked\"\n";
    hook << "    exit 1\n";
    hook << "  fi\n";
    hook << "done\n";
    
    hook.close();
    
    std::filesystem::permissions(hook_path, 
        std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec,
        std::filesystem::perm_options::add);
    
    return true;
}

} // namespace hfdown
