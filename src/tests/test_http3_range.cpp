#include "http3_client.hpp"
#include <iostream>
#include <cassert>

using namespace hfdown;

int main() {
    Http3Client client;
    const std::string url = "https://www.cloudflare.com/img/logo-cloudflare-dark.svg";
    
    std::cout << "Request 1 (Discovery)...\n";
    client.get(url);

    std::cout << "Request 2 (Range via Cache)...\n";
    auto result = client.get_with_range(url, 0, 99);
    
    if (result) {
        std::cout << "Success! Status: " << result->status_code << " Protocol: " << result->protocol << "\n";
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
