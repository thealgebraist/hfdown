#include "http3_client.hpp"
#include <iostream>

using namespace hfdown;

int main() {
    auto client = std::make_shared<Http3Client>();
    auto result = client->get("https://www.google.com/");
    if (result) {
        std::cout << "Success! Status: " << result->status_code << "\n";
        std::cout << "Body length: " << result->body.size() << "\n";
    } else {
        std::cout << "Error: " << result.error().message << "\n";
    }
    return 0;
}
