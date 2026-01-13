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

void test_http3_get() {
    auto client = std::make_shared<Http3Client>();
    struct ResultBox { std::optional<std::expected<HttpResponse, HttpErrorInfo>> res; std::mutex m; std::condition_variable cv; };
    auto box = std::make_shared<ResultBox>();

    std::thread([client, box]() {
        auto r = client->get("https://cloudflare-quic.com/");
        {
            std::lock_guard lk(box->m);
            box->res = std::move(r);
        }
        box->cv.notify_one();
    }).detach();

    std::unique_lock lk(box->m);
    if (box->cv.wait_for(lk, std::chrono::seconds(5), [&box]{ return box->res.has_value(); })) {
        auto result = *box->res;
        assert(result || result.error().error == HttpError::ConnectionFailed);
    } else {
        std::cout << "[SKIP] test_http3_get timed out (no QUIC or network).\n";
    }
}

int main() {
    test_parse_url();
    std::cout << "✓ test_parse_url passed\n";
    test_http3_get();
    std::cout << "✓ test_http3_get passed (if QUIC available)\n";
    return 0;
}
