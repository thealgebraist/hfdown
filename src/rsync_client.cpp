#include "rsync_client.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <format>
#include <openssl/sha.h>
#include <array>
#include <regex>
#include <cstdlib>

namespace hfdown {

RsyncClient::RsyncClient(std::string hf_token)
    : token_(std::move(hf_token))
    , hf_client_(token_)
{
}

std::string RsyncClient::calculate_checksum(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    
    std::array<char, 8192> buffer;
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
        SHA256_Update(&ctx, buffer.data(), file.gcount());
    }
    
    std::array<unsigned char, SHA256_DIGEST_LENGTH> hash;
    SHA256_Final(hash.data(), &ctx);
    
    std::ostringstream oss;
    for (unsigned char byte : hash) {
        oss << std::format("{:02x}", byte);
    }
    
    return oss.str();
}

bool RsyncClient::needs_download(
    const ModelFile& remote_file,
    const std::filesystem::path& local_path,
    const RsyncConfig& config)
{
    if (!std::filesystem::exists(local_path)) {
        return true; // File doesn't exist locally
    }
    
    // Check size
    if (config.check_size) {
        auto local_size = std::filesystem::file_size(local_path);
        if (local_size != remote_file.size) {
            return true; // Size mismatch
        }
    }
    
    // Check checksum (most reliable but slower)
    if (config.check_checksum && !remote_file.oid.empty()) {
        auto local_checksum = calculate_checksum(local_path);
        // HuggingFace uses Git LFS, OID is sha256 hash
        if (local_checksum != remote_file.oid) {
            return true; // Checksum mismatch
        }
    }
    
    return false; // File is up to date
}

std::expected<SyncStats, RsyncErrorInfo> RsyncClient::sync_to_local(
    const std::string& model_id,
    const std::filesystem::path& local_dir,
    const RsyncConfig& config,
    ProgressCallback progress_callback)
{
    SyncStats stats;
    
    // Get model info
    auto model_info = hf_client_.get_model_info(model_id);
    if (!model_info) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::NetworkError,
            std::format("Failed to get model info: {}", model_info.error().message)
        });
    }
    
    stats.total_files = model_info->files.size();
    
    // Create output directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(local_dir, ec);
    if (ec) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::FileSystemError,
            std::format("Failed to create directory: {}", ec.message())
        });
    }
    
    // Determine which files need to be downloaded
    std::vector<ModelFile> files_to_download;
    for (const auto& file : model_info->files) {
        auto local_path = local_dir / file.filename;
        
        if (needs_download(file, local_path, config)) {
            files_to_download.push_back(file);
            stats.bytes_to_download += file.size;
        } else {
            stats.files_unchanged++;
            if (config.verbose) {
                std::cout << std::format("Skipping {} (up to date)\n", file.filename);
            }
        }
    }
    
    stats.files_to_download = files_to_download.size();
    
    if (config.dry_run) {
        std::cout << std::format("Dry run: Would download {} files ({:.2f} MB)\n",
            files_to_download.size(),
            stats.bytes_to_download / (1024.0 * 1024.0));
        return stats;
    }
    
    // Download files that need updating
    for (const auto& file : files_to_download) {
        if (config.verbose) {
            std::cout << std::format("Downloading: {}\n", file.filename);
        }
        
        auto local_path = local_dir / file.filename;
        
        // Create parent directories if needed
        auto parent_dir = local_path.parent_path();
        if (!parent_dir.empty()) {
            std::filesystem::create_directories(parent_dir, ec);
            if (ec) {
                return std::unexpected(RsyncErrorInfo{
                    RsyncError::FileSystemError,
                    std::format("Failed to create directory {}: {}", 
                        parent_dir.string(), ec.message())
                });
            }
        }
        
        auto result = hf_client_.download_file(
            model_id,
            file.filename,
            local_path,
            progress_callback
        );
        
        if (!result) {
            return std::unexpected(RsyncErrorInfo{
                RsyncError::NetworkError,
                std::format("Failed to download {}: {}", 
                    file.filename, result.error().message)
            });
        }
        
        stats.bytes_downloaded += file.size;
    }
    
    return stats;
}

namespace {
    // Allowed characters for SSH validation
    constexpr std::string_view VALID_USERNAME_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    constexpr std::string_view VALID_HOST_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:";
}

// Escape shell special characters
std::string escape_shell_arg(const std::string& arg) {
    std::string escaped;
    escaped.reserve(arg.size() + 10);
    escaped += "'";
    for (char c : arg) {
        if (c == '\'') {
            escaped += "'\\''";  // Close quote, escape quote, open quote
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

// Validate SSH configuration
std::expected<void, RsyncErrorInfo> validate_ssh_config(const SshConfig& config) {
    // Validate port range (uint16_t max is 65535, so only check for 0)
    if (config.port == 0) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::SshConnectionFailed,
            "Invalid port number: port cannot be 0"
        });
    }
    
    // Validate username contains only safe characters
    if (config.username.empty() || 
        config.username.find_first_not_of(VALID_USERNAME_CHARS) != std::string::npos) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::SshConnectionFailed,
            "Invalid username: must contain only alphanumeric characters, underscore, or hyphen"
        });
    }
    
    // Validate host (basic check - alphanumeric, dots, hyphens, colons for IPv6)
    if (config.host.empty() || 
        config.host.find_first_not_of(VALID_HOST_CHARS) != std::string::npos) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::SshConnectionFailed,
            "Invalid host: must be a valid hostname or IP address"
        });
    }
    
    return {};
}

std::expected<std::string, RsyncErrorInfo> RsyncClient::ssh_execute(
    const SshConfig& config,
    const std::string& command)
{
    // Validate configuration first
    if (auto validation = validate_ssh_config(config); !validation) {
        return std::unexpected(validation.error());
    }
    
    // Build SSH command with escaped arguments
    std::ostringstream ssh_cmd;
    ssh_cmd << "ssh -o StrictHostKeyChecking=no -o BatchMode=yes ";
    
    if (!config.key_path.empty()) {
        ssh_cmd << "-i " << escape_shell_arg(config.key_path) << " ";
    }
    
    ssh_cmd << "-p " << config.port << " ";
    // username and host are already validated to contain only safe characters (no escaping needed)
    ssh_cmd << config.username << "@" << config.host << " ";
    ssh_cmd << escape_shell_arg(command);
    
    // Execute command and capture output
    FILE* pipe = popen(ssh_cmd.str().c_str(), "r");
    if (!pipe) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::SshConnectionFailed,
            "Failed to execute SSH command"
        });
    }
    
    std::array<char, 128> buffer;
    std::string result;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    int status = pclose(pipe);
    if (status != 0) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::RemoteCommandFailed,
            std::format("SSH command failed with status {}", status)
        });
    }
    
    return result;
}

std::expected<void, RsyncErrorInfo> RsyncClient::scp_transfer(
    const SshConfig& config,
    const std::filesystem::path& local_file,
    const std::string& remote_path)
{
    // Validate configuration first
    if (auto validation = validate_ssh_config(config); !validation) {
        return std::unexpected(validation.error());
    }
    
    // Build SCP command with escaped arguments
    std::ostringstream scp_cmd;
    scp_cmd << "scp -o StrictHostKeyChecking=no -o BatchMode=yes ";
    
    if (!config.key_path.empty()) {
        scp_cmd << "-i " << escape_shell_arg(config.key_path) << " ";
    }
    
    scp_cmd << "-P " << config.port << " ";
    scp_cmd << escape_shell_arg(local_file.string()) << " ";
    // username and host are already validated to contain only safe characters (no escaping needed)
    scp_cmd << config.username << "@" << config.host << ":" << escape_shell_arg(remote_path);
    
    int status = system(scp_cmd.str().c_str());
    if (status != 0) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::SshConnectionFailed,
            std::format("SCP transfer failed with status {}", status)
        });
    }
    
    return {};
}

std::expected<SyncStats, RsyncErrorInfo> RsyncClient::sync_to_remote(
    const std::string& model_id,
    const SshConfig& ssh_config,
    const RsyncConfig& rsync_config,
    ProgressCallback progress_callback)
{
    // Create temporary directory for downloads
    auto temp_dir = std::filesystem::temp_directory_path() / "hfdown_rsync" / model_id;
    std::error_code ec;
    std::filesystem::create_directories(temp_dir, ec);
    if (ec) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::FileSystemError,
            std::format("Failed to create temp directory: {}", ec.message())
        });
    }
    
    // First, sync to local temp directory
    auto local_sync = sync_to_local(model_id, temp_dir, rsync_config, progress_callback);
    if (!local_sync) {
        return std::unexpected(local_sync.error());
    }
    
    SyncStats stats = *local_sync;
    
    // Create remote directory
    auto mkdir_result = ssh_execute(ssh_config, 
        std::format("mkdir -p {}", ssh_config.remote_path));
    if (!mkdir_result) {
        return std::unexpected(mkdir_result.error());
    }
    
    // Get model info for file list
    auto model_info = hf_client_.get_model_info(model_id);
    if (!model_info) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::NetworkError,
            std::format("Failed to get model info: {}", model_info.error().message)
        });
    }
    
    // Transfer all files to remote
    for (const auto& file : model_info->files) {
        auto local_path = temp_dir / file.filename;
        
        if (!std::filesystem::exists(local_path)) {
            continue; // File wasn't downloaded (already up to date)
        }
        
        // Create remote subdirectories if needed
        auto remote_file_path = ssh_config.remote_path + "/" + file.filename;
        auto remote_dir = std::filesystem::path(remote_file_path).parent_path().string();
        
        if (remote_dir != ssh_config.remote_path) {
            auto mkdir_result = ssh_execute(ssh_config, 
                std::format("mkdir -p {}", remote_dir));
            if (!mkdir_result) {
                return std::unexpected(mkdir_result.error());
            }
        }
        
        if (rsync_config.verbose) {
            std::cout << std::format("Transferring {} to remote...\n", file.filename);
        }
        
        auto transfer_result = scp_transfer(ssh_config, local_path, remote_file_path);
        if (!transfer_result) {
            return std::unexpected(transfer_result.error());
        }
    }
    
    // Clean up temp directory
    std::filesystem::remove_all(temp_dir, ec);
    
    return stats;
}

std::expected<SshConfig, RsyncErrorInfo> RsyncClient::parse_vast_ssh(
    const std::string& connection_string,
    const std::string& remote_path)
{
    // Parse Vast.ai style: "ssh -p PORT root@IP" or "ssh -p PORT -i KEY root@IP"
    // Support IPv4, IPv6, and hostnames
    // Note: hyphen at end of character class to avoid ambiguity
    std::regex vast_regex(R"(ssh\s+-p\s+(\d+)(?:\s+-i\s+(\S+))?\s+([\w_\-]+)@([\w\d\.:_\-]+))");
    std::smatch matches;
    
    if (!std::regex_search(connection_string, matches, vast_regex)) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::SshConnectionFailed,
            "Invalid Vast.ai connection string format. Expected: 'ssh -p PORT [-i KEY] USER@HOST'"
        });
    }
    
    SshConfig config;
    
    // Validate and parse port
    try {
        int port_num = std::stoi(matches[1].str());
        if (port_num < 1 || port_num > 65535) {
            return std::unexpected(RsyncErrorInfo{
                RsyncError::SshConnectionFailed,
                std::format("Invalid port number: {} (must be 1-65535)", port_num)
            });
        }
        config.port = static_cast<uint16_t>(port_num);
    } catch (const std::exception& e) {
        return std::unexpected(RsyncErrorInfo{
            RsyncError::SshConnectionFailed,
            std::format("Failed to parse port number: {}", e.what())
        });
    }
    
    if (matches[2].matched) {
        config.key_path = matches[2].str();
    }
    
    config.username = matches[3].str();
    config.host = matches[4].str();
    config.remote_path = remote_path;
    
    return config;
}

} // namespace hfdown
