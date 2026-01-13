#pragma once

#include "hf_client.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <expected>

namespace hfdown {

struct RsyncConfig {
    bool check_size = true;           // Compare file sizes
    bool check_mtime = false;         // Compare modification times
    bool check_checksum = true;       // Compare checksums (slower but accurate)
    bool delete_extra = false;        // Delete files in dest not in source
    bool dry_run = false;             // Don't actually transfer, just show what would happen
    size_t parallel_downloads = 4;    // Number of parallel downloads
    bool verbose = false;             // Show detailed progress
};

struct SshConfig {
    std::string host;
    uint16_t port = 22;
    std::string username;
    std::string password;             // Optional password (prefer key auth)
    std::string key_path;             // Path to SSH private key
    std::string remote_path;          // Remote destination path
};

struct SyncStats {
    size_t total_files = 0;
    size_t files_to_download = 0;
    size_t files_unchanged = 0;
    size_t files_deleted = 0;
    size_t bytes_to_download = 0;
    size_t bytes_downloaded = 0;
};

enum class RsyncError {
    NetworkError,
    FileSystemError,
    SshConnectionFailed,
    RemoteCommandFailed,
    ChecksumMismatch,
    PermissionDenied
};

struct RsyncErrorInfo {
    RsyncError error;
    std::string message;
};

// Rsync-like client for incremental model downloads
class RsyncClient {
public:
    explicit RsyncClient(std::string hf_token = "");
    
    // Sync model to local directory (incremental)
    std::expected<SyncStats, RsyncErrorInfo> sync_to_local(
        const std::string& model_id,
        const std::filesystem::path& local_dir,
        const RsyncConfig& config = {},
        ProgressCallback progress_callback = nullptr
    );
    
    // Sync model directly to remote server via SSH
    std::expected<SyncStats, RsyncErrorInfo> sync_to_remote(
        const std::string& model_id,
        const SshConfig& ssh_config,
        const RsyncConfig& rsync_config = {},
        ProgressCallback progress_callback = nullptr
    );
    
    // Parse Vast.ai style connection string: "ssh -p PORT root@IP"
    static std::expected<SshConfig, RsyncErrorInfo> parse_vast_ssh(
        const std::string& connection_string,
        const std::string& remote_path
    );

private:
    std::string token_;
    HuggingFaceClient hf_client_;
    
    // Check if file needs to be downloaded
    bool needs_download(
        const ModelFile& remote_file,
        const std::filesystem::path& local_path,
        const RsyncConfig& config
    );
    
    // Calculate SHA256 checksum of a file
    std::string calculate_checksum(const std::filesystem::path& path);
    
    // Execute SSH command
    std::expected<std::string, RsyncErrorInfo> ssh_execute(
        const SshConfig& config,
        const std::string& command
    );
    
    // Transfer file via SCP
    std::expected<void, RsyncErrorInfo> scp_transfer(
        const SshConfig& config,
        const std::filesystem::path& local_file,
        const std::string& remote_path
    );
};

} // namespace hfdown
