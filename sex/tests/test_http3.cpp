#include "http3_client.hpp"
#include <iostream>
#include <cassert>
#include <future>
#include <chrono>

using namespace hfdown;

void test_parse_url() {
    Http3Client client;
    auto [host, port] = client.parse_url("https://example.com:4443/path");
    assert(host == "example.com");
    assert(port == 4443);
    auto [host2, port2] = client.parse_url("https://example.com");
    assert(host2 == "example.com");
    assert(port2 == 443);
    auto [host3, port3] = client.parse_url("https://example.com/");
    assert(host3 == "example.com");
    assert(port3 == 443);
}

void test_http3_discovery() {
    Http3Client client;
    std::cout << "Request 1 (Discovery): https://www.google.com/\n";
    auto r1 = client.get("https://www.google.com/");
    assert(r1 && (r1->protocol == "http/1.1" || r1->protocol == "h2"));
    assert(!r1->alt_svc.empty());
    std::cout << "✓ Discovered: " << r1->alt_svc << "\n";

    std::cout << "Request 2 (Cached H3): https://www.google.com/\n";
    auto r2 = client.get("https://www.google.com/");
    if (r2 && r2->protocol == "h3") {
        std::cout << "✓ Successfully switched to h3 from cache!\n";
    } else {
        std::cout << "✗ Failed to use H3 from cache (got " << (r2 ? r2->protocol : "error") << ")\n";
    }
}

int main() {
    test_parse_url();
    std::cout << "✓ test_parse_url passed\n";
    test_http3_discovery();
    return 0;
}
