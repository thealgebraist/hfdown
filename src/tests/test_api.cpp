#include "http_client.hpp"
#include <iostream>

int main() {
    hfdown::HttpClient client;
    auto result = client.get("https://huggingface.co/api/models/sshleifer/tiny-gpt2/tree/main");
    
    if (result) {
        std::cout << "Success! Length: " << result->length() << " bytes\n";
        std::cout << "First 500 chars:\n";
        std::cout << result->substr(0, std::min(size_t(500), result->length())) << "\n";
    } else {
        std::cout << "Error: " << result.error().message << "\n";
    }
    
    return 0;
}
