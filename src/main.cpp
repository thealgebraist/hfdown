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

enum class FSMState {
    Init,
    ParseArgs,
    PreCommand,
    RunCommand,
    PostCommand,
    Error,
    Done
};

struct FSMContext {
    int argc;
    char** argv;
    std::string cmd, token, proto, mirror;
    int threads = 4;
    std::vector<std::string> args;
    int exit_code = 0;
    std::string error_message;
    // FSM extension: add timing, logging, and result info
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::string result_message;
};

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
    FSMState state = FSMState::Init;
    FSMContext ctx{argc, argv};
    while (state != FSMState::Done) {
        switch (state) {
            case FSMState::Init:
                ctx.start_time = std::chrono::steady_clock::now();
                if (ctx.argc < 2) {
                    ctx.exit_code = 1;
                    state = FSMState::Error;
                } else {
                    ctx.cmd = ctx.argv[1];
                    state = FSMState::ParseArgs;
                }
                break;
            case FSMState::ParseArgs: {
                for (int i = 2; i < ctx.argc; ++i) {
                    std::string a = ctx.argv[i];
                    if (a == "--token" && i + 1 < ctx.argc) ctx.token = ctx.argv[++i];
                    else if (a == "--protocol" && i + 1 < ctx.argc) ctx.proto = ctx.argv[++i];
                    else if (a == "--mirror" && i + 1 < ctx.argc) ctx.mirror = ctx.argv[++i];
                    else if (a == "--threads" && i + 1 < ctx.argc) ctx.threads = std::stoi(ctx.argv[++i]);
                    else ctx.args.push_back(a);
                }
                state = FSMState::PreCommand;
                break;
            }
            case FSMState::PreCommand:
                // Pre-command hook: logging, validation, and setup for all commands
                std::cout << "[FSM] PreCommand: " << ctx.cmd << ", Args: ";
                for (const auto& a : ctx.args) std::cout << a << " ";
                std::cout << std::endl;

                // Example: validate required arguments for each command
                if (ctx.cmd == "info") {
                    if (ctx.args.empty()) {
                        ctx.exit_code = 1;
                        ctx.error_message = "info requires <model-id> argument.";
                        state = FSMState::Error;
                        break;
                    }
                } else if (ctx.cmd == "download") {
                    if (ctx.args.empty()) {
                        ctx.exit_code = 1;
                        ctx.error_message = "download requires <model-id> argument.";
                        state = FSMState::Error;
                        break;
                    }
                } else if (ctx.cmd == "file") {
                    if (ctx.args.size() < 2) {
                        ctx.exit_code = 1;
                        ctx.error_message = "file requires <model-id> and <filename> arguments.";
                        state = FSMState::Error;
                        break;
                    }
                } else if (ctx.cmd == "http3-test") {
                    if (ctx.args.empty()) {
                        ctx.exit_code = 1;
                        ctx.error_message = "http3-test requires <url> argument.";
                        state = FSMState::Error;
                        break;
                    }
                } else {
                    ctx.exit_code = 1;
                    ctx.error_message = "Unknown command: " + ctx.cmd;
                    state = FSMState::Error;
                    break;
                }

                // Example: setup or logging for all commands
                std::cout << "[FSM] PreCommand checks passed for '" << ctx.cmd << "'\n";
                state = FSMState::RunCommand;
                break;
            case FSMState::RunCommand:
                if (ctx.cmd == "info" && !ctx.args.empty()) {
                    ctx.exit_code = cmd_info(ctx.args[0], ctx.token, ctx.proto, ctx.mirror);
                    ctx.result_message = "Info command executed.";
                    state = FSMState::PostCommand;
                } else if (ctx.cmd == "download" && !ctx.args.empty()) {
                    ctx.exit_code = cmd_download(ctx.args[0], ctx.args.size() > 1 ? ctx.args[1] : ".", ctx.token, ctx.proto, ctx.mirror, ctx.threads);
                    ctx.result_message = "Download command executed.";
                    state = FSMState::PostCommand;
                } else if (ctx.cmd == "file" && ctx.args.size() > 1) {
                    ctx.exit_code = cmd_download_file(ctx.args[0], ctx.args[1], ctx.token, ctx.proto, ctx.mirror);
                    ctx.result_message = "File command executed.";
                    state = FSMState::PostCommand;
                } else if (ctx.cmd == "http3-test" && !ctx.args.empty()) {
                    ctx.exit_code = cmd_h3_test(ctx.args[0], ctx.proto);
                    ctx.result_message = "HTTP3-test command executed.";
                    state = FSMState::PostCommand;
                } else {
                    ctx.exit_code = 1;
                    ctx.error_message = "Invalid command or arguments.";
                    state = FSMState::Error;
                }
                break;
            case FSMState::PostCommand:
                ctx.end_time = std::chrono::steady_clock::now();
                {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(ctx.end_time - ctx.start_time).count();
                    std::cout << "[FSM] " << ctx.result_message << " Elapsed: " << ms << " ms\n";
                }
                state = FSMState::Done;
                break;
            case FSMState::Error:
                if (!ctx.error_message.empty()) std::cerr << "Error: " << ctx.error_message << "\n";
                print_usage(ctx.argv[0]);
                state = FSMState::Done;
                break;
            case FSMState::Done:
                break;
        }
    }
    return ctx.exit_code;
}