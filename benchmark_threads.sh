#!/bin/bash

# Benchmark script to test different thread counts
# Measures wall-clock time for downloading a model

set -e

# Configuration
MODEL_ID="sshleifer/tiny-gpt2"  
OUTPUT_DIR="benchmark_threads_temp"
BENCHMARK_RESULTS="benchmark_threads_results.txt"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "======================================"
echo "Thread Performance Benchmark"
echo "======================================"
echo ""
echo "Model: $MODEL_ID"
echo "Output: $OUTPUT_DIR"
echo ""

# Clean up function
cleanup() {
    rm -rf "$OUTPUT_DIR"
}

# Set trap to cleanup on exit
trap cleanup EXIT

# Initialize results file
echo "Threads|WallTime(s)|Speed(MB/s)" > "$BENCHMARK_RESULTS"

# Function to run benchmark
run_thread_benchmark() {
    local threads="$1"
    
    echo -e "${BLUE}Running with $threads thread(s)...${NC}"
    
    # Clean output directory
    rm -rf "$OUTPUT_DIR"
    mkdir -p "$OUTPUT_DIR"
    
    # Run the hfdown tool directly
    START_TIME=$(date +%s)
    ./build/hfdown download "$MODEL_ID" "$OUTPUT_DIR" --threads "$threads" > /dev/null
    END_TIME=$(date +%s)
    
    DURATION=$((END_TIME - START_TIME))
    
    # Calculate total size downloaded
    TOTAL_BYTES=$(du -sk "$OUTPUT_DIR" | awk '{print $1 * 1024}')
    TOTAL_MB=$(echo "scale=2; $TOTAL_BYTES / 1048576" | bc)
    
    if [ $DURATION -gt 0 ]; then
        SPEED=$(echo "scale=2; $TOTAL_MB / $DURATION" | bc)
    else
        SPEED="N/A"
    fi
    
    echo -e "${GREEN}  ✓ Time: ${DURATION}s${NC}"
    echo -e "${GREEN}  ✓ Avg Speed: ${SPEED} MB/s${NC}"
    echo ""
    
    # Save results
    echo "$threads|$DURATION|$SPEED" >> "$BENCHMARK_RESULTS"
}

# Ensure build is up to date
echo "Rebuilding..."
cd build && cmake .. > /dev/null && make -j hfdown > /dev/null
cd ..

# Run benchmarks
run_thread_benchmark 1
run_thread_benchmark 4
run_thread_benchmark 8

echo ""
echo "======================================"
echo "Benchmark Complete!"
echo "======================================"
echo ""

# Display results
column -t -s '|' "$BENCHMARK_RESULTS"
