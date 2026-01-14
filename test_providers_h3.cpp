#include "http3_client.hpp"
#include <iostream>
#include <vector>
#include <format>

using namespace hfdown;

void test_provider(const std::string& name, const std::string& url) {
    Http3Client client;
    std::cout << std::format("--- Testing {} ---\n", name);
    
    // Request 1: Discovery
    auto r1 = client.get(url);
    std::string p1 = r1 ? r1->protocol : "error";
    bool h3_advertised = r1 && !r1->alt_svc.empty() && r1->alt_svc.find("h3") != std::string::npos;
    
    std::cout << std::format("  Req 1: Protocol={:<10} Alt-Svc={}\n", 
                            p1, h3_advertised ? "YES" : "no");

    // Request 2: Use Cache
    auto r2 = client.get(url);
    std::string p2 = r2 ? r2->protocol : "error";
    
    std::cout << std::format("  Req 2: Protocol={:<10} Result={}\n", 
                            p2, (p2 == "h3") ? "UPGRADED" : "stayed " + p2);
    std::cout << "\n";
}

int main() {
    std::vector<std::pair<std::string, std::string>> providers = {
        {"Kaggle", "https://www.kaggle.com/"},
        {"HuggingFace", "https://huggingface.co/"},
        {"OpenAI", "https://api.openai.com/v1/models"},
        {"Anthropic", "https://api.anthropic.com/v1/messages"},
        {"GitHub", "https://api.github.com/"}
    };

    for (const auto& [name, url] : providers) {
        test_provider(name, url);
    }

    return 0;
}