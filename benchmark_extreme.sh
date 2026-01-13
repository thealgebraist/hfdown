#!/bin/bash

# Extended benchmark for extreme configurations
# Tests very small and very large buffer sizes to find absolute limits

set -e

MODEL_ID="sshleifer/tiny-gpt2"
OUTPUT_DIR="benchmark_extreme"
RESULTS_FILE="benchmark_extreme_results.txt"

echo "Extended Benchmark - Testing Extreme Configurations"
echo "===================================================="
echo ""

# Clean up function
cleanup() {
    rm -rf "$OUTPUT_DIR" benchmark_extreme_test
}
trap cleanup EXIT

# Function to test a configuration
test_config() {
    local name="$1"
    local buffer="$2"
    local file_buffer="$3"
    
    echo "Testing: $name (Buffer: ${buffer}KB, FileBuffer: ${file_buffer}KB)"
    
    rm -rf "$OUTPUT_DIR"
    
    cat > benchmark_extreme_test.cpp << EOF
#include "hf_client.hpp"
int main() {
    hfdown::HuggingFaceClient client;
    hfdown::HttpConfig config;
    config.buffer_size = ${buffer} * 1024;
    config.file_buffer_size = ${file_buffer} * 1024;
    config.progress_update_ms = 500;
    config.enable_http2 = true;
    config.enable_tcp_nodelay = true;
    auto result = client.download_model("${MODEL_ID}", "${OUTPUT_DIR}", nullptr, 1);
    return result ? 0 : 1;
}
EOF
    
    g++ -std=c++23 -I./include benchmark_extreme_test.cpp src/http_client.cpp src/hf_client.cpp \
        -lcurl -o benchmark_extreme_test 2>/dev/null || return 1
    
    TIME_OUTPUT=$(mktemp)
    /usr/bin/time -l ./benchmark_extreme_test 2> "$TIME_OUTPUT" > /dev/null || {
        rm -f "$TIME_OUTPUT"
        return 1
    }
    
    USER=$(grep "user" "$TIME_OUTPUT" | awk '{print $1}')
    SYS=$(grep "sys" "$TIME_OUTPUT" | awk '{print $1}')
    TOTAL=$(echo "$USER + $SYS" | bc)
    
    echo "  CPU Time: ${TOTAL}s"
    echo "$name|$buffer|$file_buffer|$TOTAL" >> "$RESULTS_FILE"
    
    rm -f "$TIME_OUTPUT"
}

# Initialize results
echo "Configuration|Buffer(KB)|FileBuffer(KB)|CPU Time(s)" > "$RESULTS_FILE"

# Test extreme small buffers
echo ""
echo "Testing Small Buffers..."
test_config "Tiny (8KB/32KB)" 8 32
test_config "Small (16KB/64KB)" 16 64
test_config "Medium (64KB/128KB)" 64 128

# Test optimal range
echo ""
echo "Testing Optimal Range..."
test_config "Optimal (512KB/1MB)" 512 1024

# Test large buffers
echo ""
echo "Testing Large Buffers..."
test_config "Large (1MB/2MB)" 1024 2048
test_config "XLarge (2MB/4MB)" 2048 4096
test_config "XXLarge (4MB/8MB)" 4096 8192

# Test asymmetric buffers
echo ""
echo "Testing Asymmetric Buffers..."
test_config "Small CURL, Large File (128KB/2MB)" 128 2048
test_config "Large CURL, Small File (2MB/128KB)" 2048 128

echo ""
echo "Results saved to: $RESULTS_FILE"
echo ""
column -t -s '|' "$RESULTS_FILE"
