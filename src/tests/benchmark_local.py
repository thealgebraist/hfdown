import os
import subprocess
import time
import signal
import sys
import random
import string

def generate_random_data(filename, size_gb):
    print(f"Generating {size_gb}GB of random data in {filename}...")
    # Using dd for fast random data generation if possible
    try:
        subprocess.run(["dd", "if=/dev/urandom", f"of={filename}", "bs=1M", f"count={size_gb * 1024}"], check=True)
    except:
        # Fallback to python if dd fails (slower)
        with open(filename, "wb") as f:
            for _ in range(size_gb * 1024):
                f.write(os.urandom(1024 * 1024))

def setup_benchmark():
    os.makedirs("benchmark_local_data/test/model/resolve/main", exist_ok=True)
    os.makedirs("benchmark_local_data/api/models/test/model", exist_ok=True)
    
    # 1. Generate 5GB file
    data_file = "benchmark_local_data/test/model/large_file.bin"
    if not os.path.exists(data_file):
        generate_random_data(data_file, 5)
    
    # 2. Create mock API response
    api_dir = "benchmark_local_data/api/models/test/model/tree"
    os.makedirs(api_dir, exist_ok=True)
    api_json = os.path.join(api_dir, "main?recursive=true")
    with open(api_json, "w") as f:
        f.write('[{"type": "file", "path": "large_file.bin", "size": 5368709120}]')

def run_benchmark():
    # Start server
    print("Starting server...")
    server_process = subprocess.Popen([sys.executable, "src/tests/test_server.py", "benchmark_local_data"], 
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for server to write port
    time.sleep(2)
    with open("server.port", "r") as f:
        port = f.read().strip()
    
    url = f"http://localhost:{port}"
    print(f"Server running at {url}")
    
    # Clean output
    if os.path.exists("benchmark_out"):
        subprocess.run(["rm", "-rf", "benchmark_out"])
    os.makedirs("benchmark_out")

    # Run hfdown with profiling
    # On macOS, /usr/bin/time -l provides detailed stats
    print("Starting download and profiling...")
    cmd = [
        "/usr/bin/time", "-l",
        "./build/hfdown", "download", "test/model", "benchmark_out",
        "--mirror", url
    ]
    
    start_time = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    end_time = time.time()
    
    duration = end_time - start_time
    print(f"Download finished in {duration:.2f} seconds")
    
    # Kill server
    server_process.terminate()
    
    # Print results
    print("\n--- Profiling Results ---")
    print(result.stderr)
    
    if result.returncode != 0:
        print("Error: hfdown failed")
        print(result.stdout)

if __name__ == "__main__":
    setup_benchmark()
    run_benchmark()
