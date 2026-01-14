#include "hf_client.hpp"
#include "kaggle_client.hpp"
#include "cache_manager.hpp"
#include "github_client.hpp"
#include "file_monitor.hpp"
#include "git_uploader.hpp"
#include "secret_scanner.hpp"
#include "http3_client.hpp"
#include "rsync_client.hpp"
#include <iostream>
#include <sstream>
#include <format>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

using namespace hfdown;

void print_usage(const char* program_name) {
    std::cout << "HuggingFace & Kaggle Downloader (C++23)\n\n"
              << "Usage: " << program_name << " <command> [options]\n\n"
              << "Commands:\n"
              << "  info <model-id>              Get model information\n"
              << "  download <model-id> [dir]    Download entire model\n"
              << "  file <model-id> <filename>   Download specific file\n"
              << "  http3-test <url>             Test HTTP/3 connectivity\n\n"
              << "Options:\n"
              << "  --token <token>              HF API token\n"
              << "  --protocol <h3|h2|http/1.1>  Force protocol version\n"
              << "  --mirror <url>               Use HF mirror URL\n";
}

void print_progress(const DownloadProgress& p) {
    static auto last_update = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() < 100 && p.downloaded_bytes < p.total_bytes) return;
    last_update = now;

    const int width = 40;
    double pct = p.percentage();
    int filled = std::clamp(static_cast<int>(width * pct / 100.0), 0, width);
    std::cout << "\r[" << std::string(filled, '=') << (filled < width ? ">" : "=") << std::string(width - filled, ' ')
              << std::format("] {:.1f}% ({:.1f} MB/s)", pct, p.speed_mbps) << std::flush;
    if (p.downloaded_bytes >= p.total_bytes) std::cout << "\n";
}

int cmd_info(const std::string& model_id, const std::string& token, const std::string& proto, const std::string& mirror) {
    HuggingFaceClient client(token);
    if (!proto.empty()) client.set_protocol(proto);
    if (!mirror.empty()) { client.use_mirror(true); client.set_mirror_url(mirror); }
    auto res = client.get_model_info(model_id);
    if (!res) { std::cerr << "Error: " << res.error().message << "\n"; return 1; }
    std::cout << std::format("Model: {}\nFiles: {}\n", res->model_id, res->files.size());
    return 0;
}

int cmd_download(const std::string& model_id, const std::string& dir, const std::string& token, const std::string& proto, const std::string& mirror, int threads) {
    HuggingFaceClient client(token);
    if (!proto.empty()) client.set_protocol(proto);
    if (!mirror.empty()) { client.use_mirror(true); client.set_mirror_url(mirror); }
    auto res = client.download_model(model_id, dir, print_progress, threads);
    if (!res) { std::cerr << "Error: " << res.error().message << "\n"; return 1; }
    return 0;
}

int cmd_download_file(const std::string& model_id, const std::string& file, const std::string& token, const std::string& proto, const std::string& mirror) {
    HuggingFaceClient client(token);
    if (!proto.empty()) client.set_protocol(proto);
    if (!mirror.empty()) { client.use_mirror(true); client.set_mirror_url(mirror); }
    auto res = client.download_file(model_id, file, file, print_progress);
    if (!res) { std::cerr << "Error: " << res.error().message << "\n"; return 1; }
    return 0;
}

int cmd_h3_test(const std::string& url, const std::string& proto) {
    Http3Client client;
    if (!proto.empty()) client.set_protocol(proto);
    auto res = client.get(url);
    if (!res) { std::cerr << "Error: " << res.error().message << "\n"; return 1; }
    std::cout << std::format("Success! Protocol: {}, Size: {}\n", res->protocol, res->body.size());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(argv[0]); return 1; }
    std::string cmd = argv[1], token, proto, mirror;
    int threads = 4;
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--token" && i + 1 < argc) token = argv[++i];
        else if (a == "--protocol" && i + 1 < argc) proto = argv[++i];
        else if (a == "--mirror" && i + 1 < argc) mirror = argv[++i];
        else if (a == "--threads" && i + 1 < argc) threads = std::stoi(argv[++i]);
        else args.push_back(a);
    }
    if (cmd == "info" && !args.empty()) return cmd_info(args[0], token, proto, mirror);
    if (cmd == "download" && !args.empty()) return cmd_download(args[0], args.size() > 1 ? args[1] : ".", token, proto, mirror, threads);
    if (cmd == "file" && args.size() > 1) return cmd_download_file(args[0], args[1], token, proto, mirror);
    if (cmd == "http3-test" && !args.empty()) return cmd_h3_test(args[0], proto);
    print_usage(argv[0]);
    return 1;
}