#include "hf_client.hpp"
#include "compact_log.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <format>

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
    int threads = 8;
    size_t buffer_size = 512 * 1024;
    std::vector<std::string> args;
    int exit_code = 0;
    std::string error_message;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::string result_message;
};

void print_usage(const char* program_name) {
    compact::Writer::print("HuggingFace & Kaggle Downloader (C++23)\n\nUsage: ");
    compact::Writer::print(program_name);
    compact::Writer::print(" <command> [options]\n\nCommands:\n"
                           "  info <model-id>              Get model information\n"
                           "  list <model-id>              List model files\n"
                           "  download <model-id> [dir]    Download entire model\n"
                           "  file <model-id> <filename>   Download specific file\n"
                           "  http3-test <url>             Test HTTP/3 connectivity\n\nOptions:\n"
                           "  --token <token>              HF API token\n"
                           "  --protocol <h3|h2|http/1.1>  Force protocol version\n"
                           "  --mirror <url>               Use HF mirror URL\n");
}

void print_progress(const DownloadProgress& p) {
    static auto last_update = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() < 100 && p.downloaded_bytes < p.total_bytes) return;
    last_update = now;

    compact::Writer::print("\r[Progress: ");
    compact::Writer::print_num(static_cast<long>(p.percentage()));
    compact::Writer::print("%] ");
    compact::Writer::print_num(static_cast<long>(p.speed_mbps));
    compact::Writer::print(" MB/s | Total: ");
    compact::Writer::print_num(static_cast<long>(p.downloaded_bytes / (1024 * 1024)));
    compact::Writer::print(" MB");
    if (!p.active_files.empty()) {
        compact::Writer::print(" | Downloading: ");
        compact::Writer::print(p.active_files);
    }
    // Clear the rest of the line (simple hack)
    compact::Writer::print("                ");
    if (p.downloaded_bytes >= p.total_bytes) compact::Writer::nl();
}

std::string format_size(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && i < 4) {
        size /= 1024;
        i++;
    }
    return std::format("{:.2f} {}", size, units[i]);
}

int cmd_info(const std::string& model_id, const std::string& token, const std::string& proto, const std::string& mirror) {
    HuggingFaceClient client(token);
    if (!proto.empty()) client.set_protocol(proto);
    if (!mirror.empty()) { client.use_mirror(true); client.set_mirror_url(mirror); }
    auto res = client.get_model_info(model_id);
    if (!res) { compact::Writer::error("Error: "); compact::Writer::error(res.error().message); compact::Writer::error("\n"); return 1; }
    compact::Writer::print("Model: "); compact::Writer::print(res->model_id); compact::Writer::nl();
    compact::Writer::print("Files: "); compact::Writer::print_num(res->files.size()); compact::Writer::nl();
    
    size_t total_size = 0;
    for (const auto& f : res->files) total_size += f.size;
    compact::Writer::print("Total Size: "); compact::Writer::print(format_size(total_size)); compact::Writer::nl();
    
    return 0;
}

int cmd_list(const std::string& model_id, const std::string& token, const std::string& proto, const std::string& mirror) {
    HuggingFaceClient client(token);
    if (!proto.empty()) client.set_protocol(proto);
    if (!mirror.empty()) { client.use_mirror(true); client.set_mirror_url(mirror); }
    auto res = client.get_model_info(model_id);
    if (!res) { compact::Writer::error("Error: "); compact::Writer::error(res.error().message); compact::Writer::error("\n"); return 1; }
    
    compact::Writer::print("Model: "); compact::Writer::print(model_id); compact::Writer::nl();
    for (const auto& file : res->files) {
        compact::Writer::print(file.filename); 
        compact::Writer::print("  "); 
        compact::Writer::print(format_size(file.size)); 
        compact::Writer::print("  "); 
        compact::Writer::print(file.oid); 
        compact::Writer::nl();
    }
    return 0;
}

int cmd_download(const std::string& model_id, const std::string& dir, const std::string& token, const std::string& proto, const std::string& mirror, int threads, size_t buffer_size) {
    HuggingFaceClient client(token);
    if (!proto.empty()) client.set_protocol(proto);
    if (!mirror.empty()) { client.use_mirror(true); client.set_mirror_url(mirror); }
    
    HttpConfig config;
    config.buffer_size = buffer_size;
    config.file_buffer_size = buffer_size * 2;
    client.set_config(config);

    auto res = client.download_model(model_id, dir, print_progress, threads);
    if (!res) { compact::Writer::error("Error: "); compact::Writer::error(res.error().message); compact::Writer::error("\n"); return 1; }
    return 0;
}

int cmd_download_file(const std::string& model_id, const std::string& file, const std::string& token, const std::string& proto, const std::string& mirror, size_t buffer_size) {
    HuggingFaceClient client(token);
    if (!proto.empty()) client.set_protocol(proto);
    if (!mirror.empty()) { client.use_mirror(true); client.set_mirror_url(mirror); }
    
    HttpConfig config;
    config.buffer_size = buffer_size;
    config.file_buffer_size = buffer_size * 2;
    client.set_config(config);

    std::filesystem::path out_path = file;
    auto res = client.download_file(model_id, file, out_path, print_progress);
    if (!res) { compact::Writer::error("Error: "); compact::Writer::error(res.error().message); compact::Writer::error("\n"); return 1; }
    return 0;
}

int cmd_h3_test(const std::string& url, const std::string& proto) {
    Http3Client client;
    if (!proto.empty()) client.set_protocol(proto);
    auto res = client.get(url);
    if (!res) { compact::Writer::error("Error: "); compact::Writer::error(res.error().message); compact::Writer::error("\n"); return 1; }
    compact::Writer::print("Success! Protocol: "); compact::Writer::print(res->protocol); 
    compact::Writer::print(", Size: "); compact::Writer::print_num(res->body.size()); compact::Writer::nl();
    return 0;
}

int main(int argc, char** argv) {
    FSMState state = FSMState::Init;
    FSMContext ctx{.argc = argc, .argv = argv};
    auto last_heartbeat = std::chrono::steady_clock::now();
    compact::Writer::error("[FSM] Entry heartbeat\n");

    while (state != FSMState::Done) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count() >= 1) {
            compact::Writer::error("[FSM] Heartbeat...\n");
            last_heartbeat = now;
        }

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
                    else if (a == "--threads" && i + 1 < ctx.argc) ctx.threads = std::stoi(argv[++i]);
                    else if (a == "--buffer-size" && i + 1 < ctx.argc) ctx.buffer_size = std::stoul(ctx.argv[++i]) * 1024;
                    else ctx.args.push_back(a);
                }
                state = FSMState::PreCommand;
                break;
            }
            case FSMState::PreCommand:
                compact::Writer::print("[FSM] PreCommand: "); compact::Writer::print(ctx.cmd); compact::Writer::nl();

                if (ctx.cmd == "info") {
                    if (ctx.args.empty()) { ctx.exit_code = 1; ctx.error_message = "info requires <model-id>"; state = FSMState::Error; break; }
                } else if (ctx.cmd == "list") {
                    if (ctx.args.empty()) { ctx.exit_code = 1; ctx.error_message = "list requires <model-id>"; state = FSMState::Error; break; }
                } else if (ctx.cmd == "download") {
                    if (ctx.args.empty()) { ctx.exit_code = 1; ctx.error_message = "download requires <model-id>"; state = FSMState::Error; break; }
                } else if (ctx.cmd == "file") {
                    if (ctx.args.size() < 2) { ctx.exit_code = 1; ctx.error_message = "file requires <model-id> and <filename>"; state = FSMState::Error; break; }
                } else if (ctx.cmd == "http3-test") {
                    if (ctx.args.empty()) { ctx.exit_code = 1; ctx.error_message = "http3-test requires <url>"; state = FSMState::Error; break; }
                } else { ctx.exit_code = 1; ctx.error_message = "Unknown command: " + ctx.cmd; state = FSMState::Error; break; }

                state = FSMState::RunCommand;
                break;
            case FSMState::RunCommand:
                if (ctx.cmd == "info" && !ctx.args.empty()) {
                    ctx.exit_code = cmd_info(ctx.args[0], ctx.token, ctx.proto, ctx.mirror);
                    ctx.result_message = "Info command executed.";
                    state = FSMState::PostCommand;
                } else if (ctx.cmd == "list" && !ctx.args.empty()) {
                    ctx.exit_code = cmd_list(ctx.args[0], ctx.token, ctx.proto, ctx.mirror);
                    ctx.result_message = "List command executed.";
                    state = FSMState::PostCommand;
                } else if (ctx.cmd == "download" && !ctx.args.empty()) {
                    ctx.exit_code = cmd_download(ctx.args[0], ctx.args.size() > 1 ? ctx.args[1] : ".", ctx.token, ctx.proto, ctx.mirror, ctx.threads, ctx.buffer_size);
                    ctx.result_message = "Download command executed.";
                    state = FSMState::PostCommand;
                } else if (ctx.cmd == "file" && ctx.args.size() > 1) {
                    ctx.exit_code = cmd_download_file(ctx.args[0], ctx.args[1], ctx.token, ctx.proto, ctx.mirror, ctx.buffer_size);
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
                compact::Writer::print("[FSM] "); compact::Writer::print(ctx.result_message); compact::Writer::nl();
                state = FSMState::Done;
                break;
            case FSMState::Error:
                if (!ctx.error_message.empty()) { compact::Writer::error("Error: "); compact::Writer::error(ctx.error_message); compact::Writer::error("\n"); }
                print_usage(ctx.argv[0]);
                state = FSMState::Done;
                break;
            case FSMState::Done:
                break;
        }
    }
    return ctx.exit_code;
}