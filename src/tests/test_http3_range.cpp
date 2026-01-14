#include "http3_client.hpp"
#include <iostream>
#include <cassert>

using namespace hfdown;

int main() {
    Http3Client client;
    // Request first 100 bytes of Google
    auto result = client.get_with_range("https://www.google.com/", 0, 99);
    
    if (result) {
        std::cout << "Success! Status: " << result->status_code << "\n";
        std::cout << "Body size: " << result->body.size() << " bytes\n";
        if (result->status_code == 206) {
            std::cout << "✓ Partial content received correctly\n";
        } else if (result->status_code == 200) {
            std::cout << "! Server returned full content (200) instead of partial (206)\n";
        }
    } else {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }
    return 0;
}
