#include "hf_client.hpp"
#include <iostream>
#include <chrono>

int main() {
    hfdown::HuggingFaceClient client;
    
    hfdown::HttpConfig config;
    config.buffer_size = 16 * 1024;
    config.file_buffer_size = 64 * 1024;
    config.progress_update_ms = 500;
    config.enable_http2 = false;
    config.enable_tcp_nodelay = false;
    config.enable_tcp_keepalive = true;
    
    auto result = client.download_model("sshleifer/tiny-gpt2", "benchmark_temp", nullptr, 1);
    
    if (!result) {
        std::cerr << "Error: " << result.error().message << std::endl;
        return 1;
    }
    
    return 0;
}
