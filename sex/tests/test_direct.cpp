#include "http_client.hpp"
#include <iostream>

int main() {
    hfdown::HttpClient client;
    auto result = client.get("https://huggingface.co/sshleifer/tiny-gpt2/resolve/main/vocab.json");
    
    if (result) {
        std::cout << "Length: " << result->length() << " bytes\n";
        if (result->length() > 0) {
            std::cout << result->substr(0, std::min(size_t(100), result->length())) << "\n";
        } else {
            std::cout << "ERROR: Empty response!\n";
        }
    } else {
        std::cout << "Error: " << result.error().message << "\n";
    }
    return 0;
}
