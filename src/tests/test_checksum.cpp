
#include <openssl/evp.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cassert>
#include <string>

std::string sha256_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[4096];
    while (file.good()) {
        file.read(buf, sizeof(buf));
        std::streamsize n = file.gcount();
        if (n > 0) EVP_DigestUpdate(ctx, buf, n);
    }
    unsigned char hash[32];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return oss.str();
}

int main(int argc, char** argv) {
    assert(argc == 3 && "Usage: test_checksum <file> <expected_sha256>");
    std::string file = argv[1];
    std::string expected = argv[2];
    std::string actual = sha256_file(file);
    std::cout << "Actual:   " << actual << "\n";
    std::cout << "Expected: " << expected << "\n";
    assert(actual == expected && "Checksum mismatch!");
    std::cout << "✓ Checksum matches\n";
    return 0;
}
