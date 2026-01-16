#include "secret_scanner.hpp"
#include <fstream>
#include <algorithm>
#include <format>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__SSE4_2__)
#include <immintrin.h>
#endif

namespace hfdown {

SecretScanner::SecretScanner() {
    init_patterns();
    init_safe_extensions();
}

void SecretScanner::init_patterns() {
    // Keep regex for confirmation, but pre-scan handles the bulk
    patterns_ = {
        {"GitHub Token", std::regex(R"(ghp_[a-zA-Z0-9]{36})"), "GitHub personal access token"},
        {"HuggingFace Token", std::regex(R"(hf_[a-zA-Z0-9]{30,})"), "HuggingFace token"},
        {"AWS Key", std::regex(R"(AKIA[0-9A-Z]{16})"), "AWS access key"},
    };
}

void SecretScanner::init_safe_extensions() {
    safe_extensions_ = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".ico",
        ".bin", ".dat", ".db", ".safetensors", ".pt", ".pth"
    };
}

// SIMD pre-scan: Checks 16 bytes at a time for 'g', 'h', 'A', 's', '-'
// High throughput, executes multiple instructions per cycle.
static inline bool simd_contains_trigger(const char* data, size_t len) {
    size_t i = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    uint8x16_t v_g = vdupq_n_u8('g');
    uint8x16_t v_h = vdupq_n_u8('h');
    uint8x16_t v_A = vdupq_n_u8('A');
    uint8x16_t v_s = vdupq_n_u8('s');
    uint8x16_t v_dash = vdupq_n_u8('-');

    for (; i + 16 <= len; i += 16) {
        uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
        uint8x16_t mask = vorrq_u8(vceqq_u8(chunk, v_g), vceqq_u8(chunk, v_h));
        mask = vorrq_u8(mask, vceqq_u8(chunk, v_A));
        mask = vorrq_u8(mask, vceqq_u8(chunk, v_s));
        mask = vorrq_u8(mask, vceqq_u8(chunk, v_dash));
        if (vaddv_u8(vget_low_u8(mask)) + vaddv_u8(vget_high_u8(mask)) > 0) return true;
    }
#elif defined(__SSE4_2__)
    __m128i v_g = _mm_set1_epi8('g');
    __m128i v_h = _mm_set1_epi8('h');
    __m128i v_A = _mm_set1_epi8('A');
    __m128i v_s = _mm_set1_epi8('s');
    __m128i v_dash = _mm_set1_epi8('-');

    for (; i + 16 <= len; i += 16) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
        __m128i mask = _mm_or_si128(_mm_cmpeq_epi8(chunk, v_g), _mm_cmpeq_epi8(chunk, v_h));
        mask = _mm_or_si128(mask, _mm_cmpeq_epi8(chunk, v_A));
        mask = _mm_or_si128(mask, _mm_cmpeq_epi8(chunk, v_s));
        mask = _mm_or_si128(mask, _mm_cmpeq_epi8(chunk, v_dash));
        if (_mm_movemask_epi8(mask) != 0) return true;
    }
#endif
    // Scalar fallback
    for (; i < len; ++i) {
        char c = data[i];
        if (c == 'g' || c == 'h' || c == 'A' || c == 's' || c == '-') return true;
    }
    return false;
}

bool SecretScanner::should_scan(const std::filesystem::path& file_path) const {
    auto ext = file_path.extension().string();
    return std::find(safe_extensions_.begin(), safe_extensions_.end(), ext) == safe_extensions_.end();
}

bool SecretScanner::has_secrets(const std::filesystem::path& file_path) const {
    if (!should_scan(file_path)) return false;
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return false;
    
    std::string line;
    while (std::getline(file, line)) {
        if (!simd_contains_trigger(line.data(), line.size())) continue;

        for (const auto& pattern : patterns_) {
            if (std::regex_search(line, pattern.pattern)) return true;
        }
    }
    return false;
}

std::vector<std::string> SecretScanner::find_secrets(const std::filesystem::path& file_path) const {
    std::vector<std::string> findings;
    if (!should_scan(file_path)) return findings;
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return findings;
    
    std::string line;
    int line_num = 1;
    while (std::getline(file, line)) {
        if (simd_contains_trigger(line.data(), line.size())) {
            for (const auto& pattern : patterns_) {
                if (std::regex_search(line, pattern.pattern)) {
                    findings.push_back(std::format("Line {}: {} detected", line_num, pattern.name));
                }
            }
        }
        ++line_num;
    }
    return findings;
}

bool SecretScanner::install_hook(const std::filesystem::path& /*repo_path*/) {
    return false; // Hook logic removed for brevity/speed
}

} // namespace hfdown