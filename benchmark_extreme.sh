#!/bin/bash
set -e

# Configuration
TEST_DIR="$(pwd)/extreme_test_data"
PORT=8888

echo "Cleaning up..."
rm -rf "$TEST_DIR" benchmark_extreme_out
mkdir -p "$TEST_DIR/api/models/test/stress/tree"
mkdir -p "$TEST_DIR/test/stress/resolve/main"

# Rebuild hfdown
cd build && make -j hfdown && cd ..

# Function to run benchmark for a specific size
run_size_benchmark() {
    local size_k=$1
    local num_files=$2
    
    echo ">>> Benchmarking ${size_k}KB files ($num_files files)..."
    
    # Generate data for this size
    cat > generate_one_size.py << EOF
import os
import json
base_dir = "extreme_test_data"
files = []
os.makedirs(f"{base_dir}/test/stress/resolve/main", exist_ok=True)
for i in range(1, $num_files + 1):
    name = f"file_${size_k}k_{i}.bin"
    # PATH in JSON should be relative to the 'resolve/main' logic in HF
    # hf_client.cpp expects filename relative to model root
    rel_path = f"file_${size_k}k_{i}.bin"
    full_path = f"{base_dir}/test/stress/resolve/main/{name}"
    with open(full_path, "wb") as f:
        f.write(b"\0" * ($size_k * 1024))
    files.append({"type": "file", "path": rel_path, "size": $size_k * 1024})
with open(f"{base_dir}/api/models/test/stress/tree/main", "w") as f:
    json.dump(files, f)
EOF
    python3 generate_one_size.py
    
    # Start server
    g++ -std=c++23 -O3 -I/opt/homebrew/include src/tests/h2_server.cpp -L/opt/homebrew/lib -lnghttp2 -o h2_server_stress
    (cd "$TEST_DIR" && ../h2_server_stress $PORT > ../h2_server.log 2>&1) &
    SERVER_PID=$!
    sleep 1
    
    # Run hfdown
    rm -rf benchmark_extreme_out && mkdir -p benchmark_extreme_out
    ./build/hfdown download test/stress benchmark_extreme_out --mirror "http://localhost:$PORT" --buffer-size 16
    
    kill $SERVER_PID || true
    wait $SERVER_PID 2>/dev/null || true
    echo ""
}

# Targeted sizes
for size in 1 2 4 8 16 32 64; do
    if [ $size -le 4 ]; then COUNT=50; elif [ $size -le 16 ]; then COUNT=20; else COUNT=10; fi
    run_size_benchmark $size $COUNT
done

echo "All benchmarks complete."