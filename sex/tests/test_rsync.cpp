#include "rsync_client.hpp"
#include <iostream>
#include <cassert>

void test_vast_ssh_parsing() {
    std::cout << "Testing Vast.ai SSH command parsing...\n\n";
    
    // Test 1: Basic format with port
    {
        auto result = hfdown::RsyncClient::parse_vast_ssh(
            "ssh -p 12345 root@1.2.3.4",
            "/workspace/models"
        );
        assert(result.has_value());
        assert(result->port == 12345);
        assert(result->username == "root");
        assert(result->host == "1.2.3.4");
        assert(result->remote_path == "/workspace/models");
        assert(result->key_path.empty());
        std::cout << "✓ Test 1 passed: Basic SSH command parsing\n";
    }
    
    // Test 2: Format with SSH key
    {
        auto result = hfdown::RsyncClient::parse_vast_ssh(
            "ssh -p 54321 -i ~/.ssh/vast_key root@192.168.1.100",
            "/models"
        );
        assert(result.has_value());
        assert(result->port == 54321);
        assert(result->username == "root");
        assert(result->host == "192.168.1.100");
        assert(result->remote_path == "/models");
        assert(result->key_path == "~/.ssh/vast_key");
        std::cout << "✓ Test 2 passed: SSH command with key path\n";
    }
    
    // Test 3: Different username
    {
        auto result = hfdown::RsyncClient::parse_vast_ssh(
            "ssh -p 22 ubuntu@10.0.0.5",
            "/home/ubuntu/data"
        );
        assert(result.has_value());
        assert(result->port == 22);
        assert(result->username == "ubuntu");
        assert(result->host == "10.0.0.5");
        assert(result->remote_path == "/home/ubuntu/data");
        std::cout << "✓ Test 3 passed: Different username parsing\n";
    }
    
    // Test 4: Hostname instead of IP
    {
        auto result = hfdown::RsyncClient::parse_vast_ssh(
            "ssh -p 2222 user@vast-server.example.com",
            "/workspace"
        );
        assert(result.has_value());
        assert(result->port == 2222);
        assert(result->username == "user");
        assert(result->host == "vast-server.example.com");
        assert(result->remote_path == "/workspace");
        std::cout << "✓ Test 4 passed: Hostname parsing\n";
    }
    
    // Test 5: IPv6 address
    {
        auto result = hfdown::RsyncClient::parse_vast_ssh(
            "ssh -p 22 root@2001:db8::1",
            "/data"
        );
        assert(result.has_value());
        assert(result->port == 22);
        assert(result->username == "root");
        assert(result->host == "2001:db8::1");
        std::cout << "✓ Test 5 passed: IPv6 address parsing\n";
    }
    
    // Test 6: Invalid port range
    {
        auto result = hfdown::RsyncClient::parse_vast_ssh(
            "ssh -p 99999 root@1.2.3.4",
            "/path"
        );
        assert(!result.has_value());
        std::cout << "✓ Test 6 passed: Invalid port rejection\n";
    }
    
    // Test 7: Invalid format
    {
        auto result = hfdown::RsyncClient::parse_vast_ssh(
            "invalid command",
            "/path"
        );
        assert(!result.has_value());
        std::cout << "✓ Test 7 passed: Invalid format rejection\n";
    }
    
    std::cout << "\n✅ All tests passed!\n";
}

int main() {
    try {
        test_vast_ssh_parsing();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
