#include "hf_client.hpp"
#include "kaggle_client.hpp"
#include "cache_manager.hpp"
#include "github_client.hpp"
#include "file_monitor.hpp"
#include "git_uploader.hpp"
#include "secret_scanner.hpp"
#include "http3_client.hpp"
#include "rsync_client.hpp"
#include "vast_monitor.hpp"
#include <iostream>
#include <sstream>
#include <format>
#include <string>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <chrono>

using namespace hfdown;

void print_usage(const char* program_name) {
    std::cout << "HuggingFace & Kaggle Downloader (C++23)\n\n";
    std::cout << "Usage:\n";
    std::cout << std::format("  {} <command> [options]\n\n", program_name);
    std::cout << "HuggingFace Commands:\n";
    std::cout << "  info <model-id>              Get information about a model\n";
    std::cout << "  download <model-id> [dir]    Download entire model to directory\n";
    std::cout << "  file <model-id> <filename>   Download a specific file from model\n\n";
    std::cout << "Rsync Commands (incremental/resumable downloads):\n";
    std::cout << "  rsync-sync <model-id> <dir>  Sync model to local dir (only download new/changed)\n";
    std::cout << "  rsync-to-vast <model-id> <ssh-cmd> <remote-path>  Sync to Vast.ai instance\n\n";
    std::cout << "HTTP/3 Commands:\n";
    std::cout << "  http3-test <url>             Test HTTP/3 connection with fallback\n";
    std::cout << "  http3-bench <url>            Benchmark HTTP/3 vs HTTP/1.1 speed\n\n";
    std::cout << "Kaggle Commands:\n";
    std::cout << "  kaggle-info <owner/dataset>  Get information about a dataset\n";
    std::cout << "  kaggle-dl <owner/dataset> [dir]  Download entire dataset\n";
    std::cout << "  kaggle-file <owner/dataset> <filename>  Download specific file\n\n";
    std::cout << "Cache Commands:\n";
    std::cout << "  cache-stats                  Show cache statistics\n";
    std::cout << "  cache-clean                  Remove unused cache entries\n\n";
    std::cout << "GitHub Commands:\n";
    std::cout << "  monitor <dir> <owner/repo>   Watch directory and upload files to GitHub\n\n";
    std::cout << "Git Commands (no token needed - uses SSH/credentials):\n";
    std::cout << "  git-push <repo-dir> <file>   Add, commit and push file using git CLI\n";
    std::cout << "  git-watch <repo-dir>         Watch repo and auto-push changes\n";
    std::cout << "  install-hook <repo-dir>      Install pre-commit hook to block secrets\n";
    std::cout << "  scan-secrets <file>          Scan file for API keys/tokens\n\n";
    std::cout << "Vast.ai Monitoring Commands:\n";
    std::cout << "  vast-monitor <ssh-cmd>       Monitor GPU/CPU resources on Vast.ai server\n\n";
    std::cout << "Options:\n";
    std::cout << "  --token <token>              HuggingFace API token (or set HF_TOKEN env var)\n";
    std::cout << "  --kaggle-user <username>     Kaggle username (or set KAGGLE_USERNAME env var)\n";
    std::cout << "  --kaggle-key <key>           Kaggle API key (or set KAGGLE_KEY env var)\n";
    std::cout << "  --github-token <token>       GitHub token (or set GITHUB_TOKEN env var)\n";
    std::cout << "  --extensions <ext,...>       File extensions to monitor (e.g., png,jpg,wav)\n";
    std::cout << "  --skip-secrets               Skip secret scanning (use for trusted files)\n";
    std::cout << "  --protocol <h3|h2|http/1.1>  Force specific HTTP protocol version\n";
    std::cout << "  --verbose                    Show detailed sync progress\n";
    std::cout << "  --dry-run                    Show what would be synced without downloading\n";
    std::cout << "  --no-checksum                Skip checksum verification (faster but less safe)\n";
    std::cout << "  --interval <seconds>         Monitoring interval (default: 5)\n";
    std::cout << "  --duration <seconds>         Monitoring duration, 0=infinite (default: 60)\n";
    std::cout << "  --output <file>              Output CSV file for monitoring data\n";
    std::cout << "  --help                       Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} info microsoft/phi-2\n", program_name);
    std::cout << std::format("  {} download gpt2 ./models/gpt2\n", program_name);
    std::cout << std::format("  {} file gpt2 config.json\n", program_name);
    std::cout << std::format("  {} rsync-sync gpt2 ./models/gpt2\n", program_name);
    std::cout << std::format("  {} rsync-to-vast gpt2 'ssh -p 12345 root@1.2.3.4' /workspace/models\n", program_name);
    std::cout << std::format("  {} kaggle-info pytorch/imagenet\n", program_name);
    std::cout << std::format("  {} kaggle-dl pytorch/imagenet ./datasets/imagenet\n", program_name);
    std::cout << std::format("  {} monitor ./outputs user/repo --extensions png,jpg,wav\n", program_name);
    std::cout << std::format("  {} vast-monitor 'ssh -p 12345 root@1.2.3.4' --interval 5 --duration 300\n", program_name);
}

void print_progress_bar(const DownloadProgress& progress) {
    const int bar_width = 50;
    double percentage = progress.percentage();
    int filled = static_cast<int>(bar_width * percentage / 100.0);
    
    std::cout << "\r[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) std::cout << "=";
        else if (i == filled) std::cout << ">";
        else std::cout << " ";
    }
    
    double mb_downloaded = progress.downloaded_bytes / (1024.0 * 1024.0);
    double mb_total = progress.total_bytes / (1024.0 * 1024.0);
    
    std::cout << std::format("] {:.1f}% ({:.1f}/{:.1f} MB) @ {:.2f} MB/s", 
                            percentage, mb_downloaded, mb_total, progress.speed_mbps);
    std::cout << std::flush;
    
    if (progress.downloaded_bytes >= progress.total_bytes && progress.total_bytes > 0) {
        std::cout << "\n";
    }
}

int cmd_info(const std::string& model_id, const std::string& token) {
    HuggingFaceClient client(token);
    
    std::cout << std::format("Fetching info for model: {}\n", model_id);
    
    auto result = client.get_model_info(model_id);
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    const auto& info = *result;
    std::cout << std::format("\nModel: {}\n", info.model_id);
    std::cout << std::format("Files: {}\n\n", info.files.size());
    
    size_t total_size = 0;
    for (const auto& file : info.files) {
        double mb = file.size / (1024.0 * 1024.0);
        std::cout << std::format("  {:50s} {:>10.2f} MB\n", file.filename, mb);
        total_size += file.size;
    }
    
    double total_mb = total_size / (1024.0 * 1024.0);
    double total_gb = total_mb / 1024.0;
    std::cout << std::format("\nTotal size: {:.2f} GB ({:.2f} MB)\n", total_gb, total_mb);
    
    return 0;
}

int cmd_download(const std::string& model_id, const std::string& output_dir, 
                const std::string& token) {
    HuggingFaceClient client(token);
    
    std::cout << std::format("Downloading model: {} to {} (4 parallel downloads)\n", 
                            model_id, output_dir);
    
    auto result = client.download_model(model_id, output_dir, print_progress_bar, 4);
    
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    return 0;
}

int cmd_download_file(const std::string& model_id, const std::string& filename,
                     const std::string& token) {
    HuggingFaceClient client(token);
    
    std::cout << std::format("Downloading {} from {}\n", filename, model_id);
    
    auto result = client.download_file(model_id, filename, filename, print_progress_bar);
    
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    std::cout << std::format("✓ Downloaded to {}\n", filename);
    return 0;
}

int cmd_kaggle_info(const std::string& dataset_id, const std::string& username, const std::string& key) {
    KaggleClient client(username, key);
    
    std::cout << std::format("Fetching info for dataset: {}\n", dataset_id);
    
    auto result = client.get_dataset_info(dataset_id);
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    const auto& info = *result;
    std::cout << std::format("\nDataset: {}/{}\n", info.owner, info.dataset);
    std::cout << std::format("Files: {}\n\n", info.files.size());
    
    for (const auto& file : info.files) {
        double mb = file.size / (1024.0 * 1024.0);
        std::cout << std::format("  {:50s} {:>10.2f} MB\n", file.name, mb);
    }
    
    double total_mb = info.total_size / (1024.0 * 1024.0);
    double total_gb = total_mb / 1024.0;
    std::cout << std::format("\nTotal size: {:.2f} GB ({:.2f} MB)\n", total_gb, total_mb);
    
    return 0;
}

int cmd_kaggle_download(const std::string& dataset_id, const std::string& output_dir,
                       const std::string& username, const std::string& key) {
    KaggleClient client(username, key);
    
    std::cout << std::format("Downloading dataset: {} to {} (4 parallel downloads)\n",
                            dataset_id, output_dir);
    
    auto result = client.download_dataset(dataset_id, output_dir, print_progress_bar, 4);
    
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    return 0;
}

int cmd_kaggle_file(const std::string& dataset_id, const std::string& filename,
                   const std::string& username, const std::string& key) {
    KaggleClient client(username, key);
    
    std::cout << std::format("Downloading {} from {}\n", filename, dataset_id);
    
    auto result = client.download_file(dataset_id, filename, filename, print_progress_bar);
    
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    std::cout << std::format("✓ Downloaded to {}\n", filename);
    return 0;
}

int cmd_cache_stats() {
    CacheManager cache;
    auto stats = cache.get_stats();
    
    std::cout << "Cache Statistics\n";
    std::cout << "================\n\n";
    std::cout << std::format("Total files:         {}\n", stats.total_files);
    std::cout << std::format("Total size:          {:.2f} MB\n", stats.total_size / (1024.0 * 1024.0));
    std::cout << std::format("Deduplicated files:  {}\n", stats.deduplicated_files);
    std::cout << std::format("Space saved:         {:.2f} MB\n\n", stats.space_saved / (1024.0 * 1024.0));
    
    if (!stats.hash_refs.empty()) {
        std::cout << "Duplicate files:\n";
        for (const auto& [hash, count] : stats.hash_refs) {
            if (count > 1) {
                std::cout << std::format("  {} ({}x)\n", hash.substr(0, 16) + "...", count);
            }
        }
    }
    
    return 0;
}

int cmd_cache_clean() {
    CacheManager cache;
    size_t removed = cache.clean_unused();
    
    std::cout << std::format("✓ Removed {} unused cache entries\n", removed);
    return 0;
}

std::vector<std::string> split_string(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (std::getline(iss, token, delimiter)) {
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

int cmd_git_push(const std::string& repo_dir, const std::string& file_path, bool skip_secrets) {
    GitUploader git(repo_dir);
    
    if (!git.is_git_repo()) {
        std::cerr << "Error: Not a git repository: " << repo_dir << "\n";
        return 1;
    }
    
    auto file = std::filesystem::path(repo_dir) / file_path;
    
    if (!skip_secrets) {
        SecretScanner scanner;
        if (scanner.has_secrets(file)) {
            std::cerr << "⚠️  Secret detected in " << file_path << " - commit blocked\n";
            std::cerr << "Use --skip-secrets to bypass this check\n";
            auto secrets = scanner.find_secrets(file);
            for (const auto& s : secrets) std::cerr << "  " << s << "\n";
            return 1;
        }
    }
    
    auto result = git.add_and_push(file, std::format("Add {}", std::filesystem::path(file_path).filename().string()));
    
    if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }
    
    std::cout << "✓ Pushed " << file_path << "\n";
    return 0;
}

int cmd_git_watch(const std::string& repo_dir, const std::vector<std::string>& extensions, bool skip_secrets) {
    GitUploader git(repo_dir);
    SecretScanner scanner;
    
    if (!git.is_git_repo()) {
        std::cerr << "Error: Not a git repository: " << repo_dir << "\n";
        return 1;
    }
    
    FileMonitor monitor(repo_dir);
    
    if (!extensions.empty()) {
        monitor.set_extensions(extensions);
        std::cout << "Monitoring extensions: ";
        for (const auto& ext : extensions) std::cout << ext << " ";
        std::cout << "\n";
    }
    
    std::cout << std::format("Watching: {} (git push)\n", repo_dir);
    std::cout << std::format("Secret scanning: {}\n", skip_secrets ? "disabled" : "enabled");
    std::cout << "Press Ctrl+C to stop...\n\n";
    
    monitor.start([&](const FileChange& change) {
        if (change.type == FileChangeType::Deleted) return;
        
        auto relative = std::filesystem::relative(change.path, repo_dir);
        std::cout << std::format("Uploading: {}... ", relative.string());
        
        if (!skip_secrets && scanner.has_secrets(change.path)) {
            std::cout << "⚠️  Secret detected - skipped\n";
            return;
        }
        
        auto result = git.add_and_push(change.path, 
                                       std::format("Update {}", change.path.filename().string()));
        
        if (result) std::cout << "✓\n";
        else std::cout << "✗ " << result.error().message << "\n";
    });
    
    return 0;
}

int cmd_install_hook(const std::string& repo_dir) {
    if (SecretScanner::install_hook(repo_dir)) {
        std::cout << "✓ Installed pre-commit hook in " << repo_dir << "/.git/hooks/pre-commit\n";
        std::cout << "  This will block commits containing secrets\n";
        return 0;
    }
    std::cerr << "Error: Failed to install hook\n";
    return 1;
}

int cmd_scan_secrets(const std::string& file_path) {
    SecretScanner scanner;
    auto path = std::filesystem::path(file_path);
    
    if (!std::filesystem::exists(path)) {
        std::cerr << "Error: File not found: " << file_path << "\n";
        return 1;
    }
    
    auto secrets = scanner.find_secrets(path);
    
    if (secrets.empty()) {
        std::cout << "✓ No secrets detected in " << file_path << "\n";
        return 0;
    }
    
    std::cout << "⚠️  Secrets detected in " << file_path << ":\n";
    for (const auto& s : secrets) {
        std::cout << "  " << s << "\n";
    }
    
    return 1;
}

int cmd_monitor(const std::string& watch_dir, const std::string& repo_id, 
               const std::string& github_token, const std::vector<std::string>& extensions) {
    auto slash_pos = repo_id.find('/');
    if (slash_pos == std::string::npos) {
        std::cerr << "Error: repo-id must be in format 'owner/repo'\n";
        return 1;
    }
    
    std::string owner = repo_id.substr(0, slash_pos);
    std::string repo = repo_id.substr(slash_pos + 1);
    
    GitHubClient github(github_token, owner, repo);
    FileMonitor monitor(watch_dir);
    
    if (!extensions.empty()) {
        monitor.set_extensions(extensions);
        std::cout << "Monitoring extensions: ";
        for (const auto& ext : extensions) std::cout << ext << " ";
        std::cout << "\n";
    }
    
    std::cout << std::format("Watching: {} → {}/{}\n", watch_dir, owner, repo);
    std::cout << "Press Ctrl+C to stop...\n\n";
    
    monitor.start([&](const FileChange& change) {
        std::string change_type = change.type == FileChangeType::Added ? "Added" :
                                 change.type == FileChangeType::Modified ? "Modified" : "Deleted";
        
        std::cout << std::format("[{}] {}\n", change_type, change.path.filename().string());
        
        if (change.type != FileChangeType::Deleted) {
            auto relative = std::filesystem::relative(change.path, watch_dir);
            auto result = github.upload_file(change.path, relative.string(), 
                                           std::format("Upload {}", change.path.filename().string()));
            
            if (result) {
                std::cout << std::format("  ✓ Uploaded to {}/{}\n", owner, repo);
            } else {
                std::cout << std::format("  ✗ Upload failed: {}\n", result.error().message);
            }
        }
    });
    
    return 0;
}

int cmd_http3_test(const std::string& url, const std::string& protocol) {
    Http3Client client;
    
    if (!protocol.empty()) {
        client.set_protocol(protocol);
        std::cout << std::format("Testing with forced protocol: {}\n", protocol);
    } else {
        std::cout << "Testing with automatic protocol negotiation (HTTP/3 → HTTP/2 → HTTP/1.1)\n";
    }
    
    std::cout << std::format("Fetching: {}\n", url);
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = client.get(url);
    auto end = std::chrono::high_resolution_clock::now();
    
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << std::format("✓ Success in {}ms\n", duration.count());
    std::cout << std::format("  Status: {}\n", result->status_code);
    std::cout << std::format("  Body size: {} bytes\n", result->body.size());
    
    return 0;
}

int cmd_http3_bench(const std::string& url) {
    std::cout << "Benchmarking HTTP protocols...\n\n";
    
    // Test HTTP/3
    Http3Client h3_client;
    h3_client.set_protocol("h3");
    
    std::cout << "[1/2] Testing HTTP/3 (QUIC)...\n";
    auto h3_start = std::chrono::high_resolution_clock::now();
    auto h3_result = h3_client.get(url);
    auto h3_end = std::chrono::high_resolution_clock::now();
    auto h3_duration = std::chrono::duration_cast<std::chrono::milliseconds>(h3_end - h3_start);
    
    // Test HTTP/1.1
    Http3Client h1_client;
    h1_client.set_protocol("http/1.1");
    
    std::cout << "[2/2] Testing HTTP/1.1 (TCP+TLS)...\n\n";
    auto h1_start = std::chrono::high_resolution_clock::now();
    auto h1_result = h1_client.get(url);
    auto h1_end = std::chrono::high_resolution_clock::now();
    auto h1_duration = std::chrono::duration_cast<std::chrono::milliseconds>(h1_end - h1_start);
    
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "RESULTS:\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    if (h3_result) {
        std::cout << std::format("HTTP/3:   {}ms ✓\n", h3_duration.count());
    } else {
        std::cout << std::format("HTTP/3:   FAILED ({})\n", h3_result.error().message);
    }
    
    if (h1_result) {
        std::cout << std::format("HTTP/1.1: {}ms ✓\n", h1_duration.count());
    } else {
        std::cout << std::format("HTTP/1.1: FAILED ({})\n", h1_result.error().message);
    }
    
    if (h3_result && h1_result) {
        auto speedup = static_cast<double>(h1_duration.count()) / h3_duration.count();
        std::cout << std::format("\nSpeedup: {:.2f}x\n", speedup);
    }
    
    return 0;
}

int cmd_rsync_sync(const std::string& model_id, const std::string& output_dir,
                  const std::string& token, const RsyncConfig& config) {
    RsyncClient rsync_client(token);
    
    std::cout << std::format("Syncing model: {} to {}\n", model_id, output_dir);
    if (config.dry_run) {
        std::cout << "DRY RUN MODE - No files will be downloaded\n";
    }
    
    auto result = rsync_client.sync_to_local(model_id, output_dir, config, print_progress_bar);
    
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    const auto& stats = *result;
    std::cout << "\nSync Summary:\n";
    std::cout << std::format("  Total files:      {}\n", stats.total_files);
    std::cout << std::format("  Files unchanged:  {}\n", stats.files_unchanged);
    std::cout << std::format("  Files downloaded: {}\n", stats.files_to_download);
    std::cout << std::format("  Bytes downloaded: {:.2f} MB\n", 
                            stats.bytes_downloaded / (1024.0 * 1024.0));
    
    return 0;
}

int cmd_rsync_to_vast(const std::string& model_id, const std::string& ssh_cmd,
                     const std::string& remote_path, const std::string& token,
                     const RsyncConfig& config) {
    RsyncClient rsync_client(token);
    
    // Parse Vast.ai SSH command
    auto ssh_config = RsyncClient::parse_vast_ssh(ssh_cmd, remote_path);
    if (!ssh_config) {
        std::cerr << std::format("Error: {}\n", ssh_config.error().message);
        std::cerr << "Expected format: 'ssh -p PORT root@IP' or 'ssh -p PORT -i KEY root@IP'\n";
        return 1;
    }
    
    std::cout << std::format("Syncing model: {} to {}@{}:{}\n",
                            model_id, ssh_config->username, ssh_config->host, 
                            ssh_config->remote_path);
    
    if (config.dry_run) {
        std::cout << "DRY RUN MODE - No files will be transferred\n";
    }
    
    auto result = rsync_client.sync_to_remote(model_id, *ssh_config, config, print_progress_bar);
    
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    const auto& stats = *result;
    std::cout << "\nSync Summary:\n";
    std::cout << std::format("  Total files:      {}\n", stats.total_files);
    std::cout << std::format("  Files unchanged:  {}\n", stats.files_unchanged);
    std::cout << std::format("  Files transferred: {}\n", stats.files_to_download);
    std::cout << std::format("  Bytes transferred: {:.2f} MB\n", 
                            stats.bytes_downloaded / (1024.0 * 1024.0));
    
    return 0;
}

int cmd_vast_monitor(const std::string& ssh_cmd, int interval, int duration, 
                     const std::string& output_file) {
    VastMonitor monitor;
    
    MonitorConfig config;
    config.ssh_command = ssh_cmd;
    config.interval_seconds = interval;
    config.duration_seconds = duration;
    config.output_file = output_file.empty() ? "vast_monitor.csv" : output_file;
    config.show_realtime = true;
    config.include_cpu = true;
    config.include_gpu = true;
    
    auto result = monitor.start_monitoring(config);
    
    if (!result) {
        std::cerr << std::format("Error: {}\n", result.error().message);
        return 1;
    }
    
    std::cout << "\nTo visualize the data, run:\n";
    std::cout << std::format("  python3 visualize_monitor.py {}\n", config.output_file.string());
    
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "--help" || command == "-h") {
        print_usage(argv[0]);
        return 0;
    }
    
    // Get token from environment or command line
    std::string token;
    if (const char* env_token = std::getenv("HF_TOKEN")) {
        token = env_token;
    }
    
    // Get Kaggle credentials
    std::string kaggle_user, kaggle_key;
    if (const char* env_user = std::getenv("KAGGLE_USERNAME")) {
        kaggle_user = env_user;
    }
    if (const char* env_key = std::getenv("KAGGLE_KEY")) {
        kaggle_key = env_key;
    }
    
    // Get GitHub token
    std::string github_token;
    if (const char* env_token = std::getenv("GITHUB_TOKEN")) {
        github_token = env_token;
    }
    
    // Parse command line arguments
    std::vector<std::string> args;
    std::vector<std::string> extensions;
    bool skip_secrets = false;
    std::string protocol;
    bool verbose = false;
    bool dry_run = false;
    bool no_checksum = false;
    int interval = 5;
    int duration = 60;
    std::string output_file;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--token" && i + 1 < argc) {
            token = argv[++i];
        } else if (arg == "--kaggle-user" && i + 1 < argc) {
            kaggle_user = argv[++i];
        } else if (arg == "--kaggle-key" && i + 1 < argc) {
            kaggle_key = argv[++i];
        } else if (arg == "--github-token" && i + 1 < argc) {
            github_token = argv[++i];
        } else if (arg == "--extensions" && i + 1 < argc) {
            extensions = split_string(argv[++i], ',');
        } else if (arg == "--skip-secrets") {
            skip_secrets = true;
        } else if (arg == "--protocol" && i + 1 < argc) {
            protocol = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--no-checksum") {
            no_checksum = true;
        } else if (arg == "--interval" && i + 1 < argc) {
            interval = std::stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        } else {
            args.push_back(arg);
        }
    }
    
    if (command == "info") {
        if (args.empty()) {
            std::cerr << "Error: model-id required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_info(args[0], token);
    }
    else if (command == "download") {
        if (args.empty()) {
            std::cerr << "Error: model-id required\n";
            print_usage(argv[0]);
            return 1;
        }
        std::string model_id = args[0];
        std::string output_dir = args.size() > 1 ? args[1] : std::format("./{}", model_id);
        return cmd_download(model_id, output_dir, token);
    }
    else if (command == "file") {
        if (args.size() < 2) {
            std::cerr << "Error: model-id and filename required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_download_file(args[0], args[1], token);
    }
    else if (command == "kaggle-info") {
        if (args.empty()) {
            std::cerr << "Error: dataset-id required (format: owner/dataset)\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_kaggle_info(args[0], kaggle_user, kaggle_key);
    }
    else if (command == "kaggle-dl") {
        if (args.empty()) {
            std::cerr << "Error: dataset-id required (format: owner/dataset)\n";
            print_usage(argv[0]);
            return 1;
        }
        std::string dataset_id = args[0];
        std::string output_dir = args.size() > 1 ? args[1] : std::format("./{}", dataset_id);
        return cmd_kaggle_download(dataset_id, output_dir, kaggle_user, kaggle_key);
    }
    else if (command == "kaggle-file") {
        if (args.size() < 2) {
            std::cerr << "Error: dataset-id and filename required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_kaggle_file(args[0], args[1], kaggle_user, kaggle_key);
    }
    else if (command == "cache-stats") {
        return cmd_cache_stats();
    }
    else if (command == "cache-clean") {
        return cmd_cache_clean();
    }
    else if (command == "git-push") {
        if (args.size() < 2) {
            std::cerr << "Error: repo-dir and file path required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_git_push(args[0], args[1], skip_secrets);
    }
    else if (command == "git-watch") {
        if (args.empty()) {
            std::cerr << "Error: repo-dir required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_git_watch(args[0], extensions, skip_secrets);
    }
    else if (command == "install-hook") {
        if (args.empty()) {
            std::cerr << "Error: repo-dir required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_install_hook(args[0]);
    }
    else if (command == "scan-secrets") {
        if (args.empty()) {
            std::cerr << "Error: file path required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_scan_secrets(args[0]);
    }
    else if (command == "monitor") {
        if (args.size() < 2) {
            std::cerr << "Error: directory and repo-id required (format: owner/repo)\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_monitor(args[0], args[1], github_token, extensions);
    }
    else if (command == "rsync-sync") {
        if (args.size() < 2) {
            std::cerr << "Error: model-id and output directory required\n";
            print_usage(argv[0]);
            return 1;
        }
        RsyncConfig config;
        config.verbose = verbose;
        config.dry_run = dry_run;
        config.check_checksum = !no_checksum;
        return cmd_rsync_sync(args[0], args[1], token, config);
    }
    else if (command == "rsync-to-vast") {
        if (args.size() < 3) {
            std::cerr << "Error: model-id, ssh-command, and remote-path required\n";
            print_usage(argv[0]);
            return 1;
        }
        RsyncConfig config;
        config.verbose = verbose;
        config.dry_run = dry_run;
        config.check_checksum = !no_checksum;
        return cmd_rsync_to_vast(args[0], args[1], args[2], token, config);
    }
    else if (command == "http3-test") {
        if (args.empty()) {
            std::cerr << "Error: URL required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_http3_test(args[0], protocol);
    }
    else if (command == "http3-bench") {
        if (args.empty()) {
            std::cerr << "Error: URL required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_http3_bench(args[0]);
    }
    else if (command == "vast-monitor") {
        if (args.empty()) {
            std::cerr << "Error: SSH command required\n";
            print_usage(argv[0]);
            return 1;
        }
        return cmd_vast_monitor(args[0], interval, duration, output_file);
    }
    else {
        std::cerr << std::format("Unknown command: {}\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
