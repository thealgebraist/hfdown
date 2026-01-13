#include "http_client.hpp"
#include <iostream>

int main() {
    hfdown::HttpClient client;
    auto result = client.get("http://httpbin.org/get");
    
    if (result) {
        std::cout << "Success! Response length: " << result->length() << " bytes\n";
        std::cout << result->substr(0, 200) << "...\n";
    } else {
        std::cout << "Error: " << result.error().message << "\n";
    }
    
    return 0;
}
