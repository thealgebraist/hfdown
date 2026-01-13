#include "http_client.hpp"
#include <iostream>

int main() {
    hfdown::HttpClient client;
    auto result = client.get("https://httpbin.org/get");
    
    if (result) {
        std::cout << "Success! Response length: " << result->length() << " bytes\n";
        if (result->length() > 0) {
            std::cout << result->substr(0, std::min(size_t(300), result->length())) << "\n";
        }
    } else {
        std::cout << "Error: " << result.error().message << "\n";
    }
    
    return 0;
}
