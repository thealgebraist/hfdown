#include "http_client.hpp"
#include <iostream>
#include <chrono>
#include <format>
#include <sys/resource.h>

using namespace hfdown;

struct BenchResult {
    double wall_time;
    double user_time;
    double sys_time;
    size_t body_size;
};

BenchResult bench_h2(const std::string& url) {
    HttpClient client;
    // Note: This now uses our custom H1.1 client, not libcurl H2
    // Naming kept for compatibility with existing calls
    
    struct rusage usage_start, usage_end;
    getrusage(RUSAGE_SELF, &usage_start);
    auto start = std::chrono::high_resolution_clock::now();
    
    auto result = client.get(url);
    
    auto end = std::chrono::high_resolution_clock::now();
    getrusage(RUSAGE_SELF, &usage_end);
    
    size_t size = 0;
    if (result) {
        size = result->size();
    } else {
        std::cerr << "HTTP request failed: " << result.error().message << "\n";
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
    
    std::cout << std::format("Benchmarking URL: {}\n\n", url);
    
    // Warmup
    bench_h2(url);
    
    std::cout << std::format("{:<10} | {:<10} | {:<10} | {:<10} | {:<10}\n", "Proto", "Wall (s)", "User (s)", "Sys (s)", "Size");
    std::cout << "-----------|------------|------------|------------|-----------\n";
    
    auto r2 = bench_h2(url);
    std::cout << std::format("{:<10} | {:<10.4f} | {:<10.4f} | {:<10.4f} | {:<10}\n", "Custom", r2.wall_time, r2.user_time, r2.sys_time, r2.body_size);
    
    return 0;
}
