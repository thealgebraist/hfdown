```cpp
#include "http3_client.hpp"
#include <iostream>
#include <chrono>
#include <curl/curl.h>
#include <sys/resource.h>
#include <format>
#include <vector>

using namespace hfdown;

struct BenchResult {
    double wall_time;
    double user_time;
    double sys_time;
    size_t body_size;
};

size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* body = static_cast<std::string*>(userp);
    body->append(static_cast<char*>(contents), realsize);
    return realsize;
}

BenchResult bench_h2(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string body;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    struct rusage usage_start, usage_end;
    getrusage(RUSAGE_SELF, &usage_start);
    auto start = std::chrono::high_resolution_clock::now();
    
    CURLcode res = curl_easy_perform(curl);
    
    auto end = std::chrono::high_resolution_clock::now();
    getrusage(RUSAGE_SELF, &usage_end);
    
    if (res != CURLE_OK) {
        std::cerr << "HTTP/2 failed: " << curl_easy_strerror(res) << "\n";
    }
    
    curl_easy_cleanup(curl);
    
    double wall = std::chrono::duration<double>(end - start).count();
    double user = (usage_end.ru_utime.tv_sec - usage_start.ru_utime.tv_sec) + 
                  (usage_end.ru_utime.tv_usec - usage_start.ru_utime.tv_usec) / 1e6;
    double sys = (usage_end.ru_stime.tv_sec - usage_start.ru_stime.tv_sec) + 
                 (usage_end.ru_stime.tv_usec - usage_start.ru_stime.tv_usec) / 1e6;
    
    return {wall, user, sys, body.size()};
}

BenchResult bench_h3(const std::string& url) {
    Http3Client client;
    client.set_protocol("h3");
    
    struct rusage usage_start, usage_end;
    getrusage(RUSAGE_SELF, &usage_start);
    auto start = std::chrono::high_resolution_clock::now();
    
    auto result = client.get(url);
    
    auto end = std::chrono::high_resolution_clock::now();
    getrusage(RUSAGE_SELF, &usage_end);
    
    size_t size = 0;
    if (result) {
        size = result->body.size();
    } else {
        std::cerr << "HTTP/3 failed: " << result.error().message << "\n";
    }
    
    double wall = std::chrono::duration<double>(end - start).count();
    double user = (usage_end.ru_utime.tv_sec - usage_start.ru_utime.tv_sec) + 
                  (usage_end.ru_utime.tv_usec - usage_start.ru_utime.tv_usec) / 1e6;
    double sys = (usage_end.ru_stime.tv_sec - usage_start.ru_stime.tv_sec) + 
                 (usage_end.ru_stime.tv_usec - usage_start.ru_stime.tv_usec) / 1e6;
    
    return {wall, user, sys, size};
}

int main(int argc, char** argv) {
    std::string url = (argc > 1) ? argv[1] : "https://www.google.com/";
    
    curl_global_init(CURL_GLOBAL_ALL);
    
    std::cout << std::format("Benchmarking URL: {}\n\n", url);
    
    // Warmup
    bench_h2(url);
    bench_h3(url);
    
    std::cout << std::format("{:<10} | {:<10} | {:<10} | {:<10} | {:<10}\n", "Proto", "Wall (s)", "User (s)", "Sys (s)", "Size");
    std::cout << "-----------|------------|------------|------------|-----------\\n";
    
    auto r2 = bench_h2(url);
    std::cout << std::format("{:<10} | {:<10.4f} | {:<10.4f} | {:<10.4f} | {:<10}\n", "HTTP/2", r2.wall_time, r2.user_time, r2.sys_time, r2.body_size);
    
    auto r3 = bench_h3(url);
    std::cout << std::format("{:<10} | {:<10.4f} | {:<10.4f} | {:<10.4f} | {:<10}\n", "HTTP/3", r3.wall_time, r3.user_time, r3.sys_time, r3.body_size);
    
    curl_global_cleanup();
    return 0;
}

```
