#include "hf_client.hpp"
#include "kaggle_client.hpp"
#include <iostream>
#include <format>
#include <string>
#include <cstring>
#include <cstdlib>
#include <iomanip>

using namespace hfdown;

void print_usage(const char* program_name) {
    std::cout << "HuggingFace & Kaggle Downloader (C++23)\n\n";
    std::cout << "Usage:\n";
    std::cout << std::format("  {} <command> [options]\n\n", program_name);
    std::cout << "HuggingFace Commands:\n";
    std::cout << "  info <model-id>              Get information about a model\n";
    std::cout << "  download <model-id> [dir]    Download entire model to directory\n";
    std::cout << "  file <model-id> <filename>   Download a specific file from model\n\n";
    std::cout << "Kaggle Commands:\n";
    std::cout << "  kaggle-info <owner/dataset>  Get information about a dataset\n";
    std::cout << "  kaggle-dl <owner/dataset> [dir]  Download entire dataset\n";
    std::cout << "  kaggle-file <owner/dataset> <filename>  Download specific file\n\n";
    std::cout << "Options:\n";
    std::cout << "  --token <token>              HuggingFace API token (or set HF_TOKEN env var)\n";
    std::cout << "  --kaggle-user <username>     Kaggle username (or set KAGGLE_USERNAME env var)\n";
    std::cout << "  --kaggle-key <key>           Kaggle API key (or set KAGGLE_KEY env var)\n";
    std::cout << "  --help                       Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} info microsoft/phi-2\n", program_name);
    std::cout << std::format("  {} download gpt2 ./models/gpt2\n", program_name);
    std::cout << std::format("  {} file gpt2 config.json\n", program_name);
    std::cout << std::format("  {} kaggle-info pytorch/imagenet\n", program_name);
    std::cout << std::format("  {} kaggle-dl pytorch/imagenet ./datasets/imagenet\n", program_name);
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
    
    // Parse command line arguments
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--token" && i + 1 < argc) {
            token = argv[++i];
        } else if (arg == "--kaggle-user" && i + 1 < argc) {
            kaggle_user = argv[++i];
        } else if (arg == "--kaggle-key" && i + 1 < argc) {
            kaggle_key = argv[++i];
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
    else {
        std::cerr << std::format("Unknown command: {}\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
