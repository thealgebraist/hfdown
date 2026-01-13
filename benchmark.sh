#!/bin/bash

# Benchmark script to test different HTTP client configurations
# Measures CPU time usage for various settings

set -e

# Configuration
MODEL_ID="sshleifer/tiny-gpt2"  # Small model for quick testing
OUTPUT_DIR="benchmark_temp"
BENCHMARK_RESULTS="benchmark_results.txt"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "======================================"
echo "HTTP Client Performance Benchmark"
echo "======================================"
echo ""
echo "Model: $MODEL_ID"
echo "Output: $OUTPUT_DIR"
echo ""

# Clean up function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    rm -rf "$OUTPUT_DIR"
}

# Set trap to cleanup on exit
trap cleanup EXIT

# Function to run benchmark
run_benchmark() {
    local test_name="$1"
    local buffer_size="$2"
    local file_buffer_size="$3"
    local progress_interval="$4"
    local http2="$5"
    local tcp_nodelay="$6"
    
    echo -e "${BLUE}Running: $test_name${NC}"
    echo "  Buffer: ${buffer_size}KB, File Buffer: ${file_buffer_size}KB, Progress: ${progress_interval}ms"
    echo "  HTTP/2: $http2, TCP_NODELAY: $tcp_nodelay"
    
    # Clean output directory
    rm -rf "$OUTPUT_DIR"
    
    # Create benchmark program that uses these settings
    cat > benchmark_test.cpp << EOF
#include "hf_client.hpp"
#include <iostream>
#include <chrono>

int main() {
    hfdown::HuggingFaceClient client;
    
    hfdown::HttpConfig config;
    config.buffer_size = ${buffer_size} * 1024;
    config.file_buffer_size = ${file_buffer_size} * 1024;
    config.progress_update_ms = ${progress_interval};
    config.enable_http2 = ${http2};
    config.enable_tcp_nodelay = ${tcp_nodelay};
    config.enable_tcp_keepalive = true;
    
    auto result = client.download_model("${MODEL_ID}", "${OUTPUT_DIR}", nullptr, 1);
    
    if (!result) {
        std::cerr << "Error: " << result.error().message << std::endl;
        return 1;
    }
    
    return 0;
}
EOF
    
    # Compile the benchmark
    g++ -std=c++23 -I./include benchmark_test.cpp src/http_client.cpp src/hf_client.cpp \
        -lcurl -o benchmark_test 2>/dev/null || {
        echo -e "${RED}Compilation failed${NC}"
        return 1
    }
    
    # Run with time and capture CPU metrics
    echo "  Downloading..."
    
    # Use /usr/bin/time to get detailed CPU usage
    # -l flag gives detailed stats on macOS
    TIME_OUTPUT=$(mktemp)
    /usr/bin/time -l ./benchmark_test 2> "$TIME_OUTPUT" > /dev/null || {
        echo -e "${RED}Download failed${NC}"
        rm -f "$TIME_OUTPUT"
        return 1
    }
    
    # Extract CPU time (user + system)
    USER_TIME=$(grep "user" "$TIME_OUTPUT" | awk '{print $1}')
    SYS_TIME=$(grep "sys" "$TIME_OUTPUT" | awk '{print $1}')
    MAX_RSS=$(grep "maximum resident set size" "$TIME_OUTPUT" | awk '{print $1}')
    
    # Calculate total CPU time
    TOTAL_CPU=$(echo "$USER_TIME + $SYS_TIME" | bc)
    
    # Convert RSS from bytes to MB
    MAX_RSS_MB=$(echo "scale=2; $MAX_RSS / 1048576" | bc)
    
    echo -e "${GREEN}  ✓ User Time: ${USER_TIME}s${NC}"
    echo -e "${GREEN}  ✓ System Time: ${SYS_TIME}s${NC}"
    echo -e "${GREEN}  ✓ Total CPU Time: ${TOTAL_CPU}s${NC}"
    echo -e "${GREEN}  ✓ Max Memory: ${MAX_RSS_MB} MB${NC}"
    echo ""
    
    # Save results
    echo "$test_name|$buffer_size|$file_buffer_size|$progress_interval|$http2|$tcp_nodelay|$USER_TIME|$SYS_TIME|$TOTAL_CPU|$MAX_RSS_MB" >> "$BENCHMARK_RESULTS"
    
    # Cleanup
    rm -f "$TIME_OUTPUT" benchmark_test
}

# Initialize results file
echo "Test Name|Buffer(KB)|FileBuffer(KB)|Progress(ms)|HTTP/2|TCP_NODELAY|User(s)|System(s)|Total(s)|Memory(MB)" > "$BENCHMARK_RESULTS"

# Run benchmarks with different configurations
echo "Starting benchmarks..."
echo ""

# Test 1: Baseline (small buffers, HTTP/1.1)
run_benchmark "Baseline (HTTP/1.1, small buffers)" 16 64 500 false false

# Test 2: HTTP/2 only
run_benchmark "HTTP/2 enabled" 16 64 500 true false

# Test 3: Large CURL buffer
run_benchmark "Large CURL buffer (512KB)" 512 64 500 true false

# Test 4: Large file buffer
run_benchmark "Large file buffer (1MB)" 512 1024 500 true false

# Test 5: Huge CURL buffer (1MB)
run_benchmark "Huge CURL buffer (1MB)" 1024 1024 500 true false

# Test 6: Massive CURL buffer (2MB)
run_benchmark "Massive CURL buffer (2MB)" 2048 2048 500 true false

# Test 7: Less frequent progress updates
run_benchmark "Slow progress (1000ms)" 512 1024 1000 true false

# Test 8: TCP_NODELAY enabled
run_benchmark "TCP_NODELAY enabled" 512 1024 500 true true

# Test 9: Optimal (current default)
run_benchmark "Current default config" 512 1024 250 true true

# Test 10: Ultra optimized
run_benchmark "Ultra optimized (2MB buffers)" 2048 2048 1000 true true

echo ""
echo "======================================"
echo "Benchmark Complete!"
echo "======================================"
echo ""

# Display results in a nice table
echo "Results Summary:"
echo ""
column -t -s '|' "$BENCHMARK_RESULTS"

echo ""
echo -e "${YELLOW}Full results saved to: $BENCHMARK_RESULTS${NC}"

# Find the best configuration
echo ""
echo "Analysis:"
BEST_CONFIG=$(tail -n +2 "$BENCHMARK_RESULTS" | sort -t'|' -k9 -n | head -n 1)
BEST_NAME=$(echo "$BEST_CONFIG" | cut -d'|' -f1)
BEST_TIME=$(echo "$BEST_CONFIG" | cut -d'|' -f9)

echo -e "${GREEN}Best Configuration: $BEST_NAME${NC}"
echo -e "${GREEN}CPU Time: ${BEST_TIME}s${NC}"
