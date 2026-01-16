import os
import subprocess
import time
import sys

def generate_random_data(filename, size_gb):
    if os.path.exists(filename) and os.path.getsize(filename) == size_gb * 1024 * 1024 * 1024:
        return
    print(f"Generating {size_gb}GB of random data in {filename}...")
    subprocess.run(["dd", "if=/dev/urandom", f"of={filename}", "bs=1M", f"count={size_gb * 1024}"], check=True)

def setup_benchmark():
    os.makedirs("benchmark_local_data/test/model/resolve/main", exist_ok=True)
    api_dir = "benchmark_local_data/api/models/test/model/tree"
    os.makedirs(api_dir, exist_ok=True)
    
    data_file = "benchmark_local_data/test/model/large_file.bin"
    generate_random_data(data_file, 5)
    
    api_json = os.path.join(api_dir, "main?recursive=true")
    with open(api_json, "w") as f:
        f.write('[{"type": "file", "path": "large_file.bin", "size": 5368709120}]')

def run_test(buffer_size_kb, mirror_url):
    print(f"\n>>> Testing Buffer Size: {buffer_size_kb} KB")
    
    # We need to modify the hfdown code or pass it via env if it supported it.
    # Since we can't easily change it without recompile, let's create a temp main.cpp
    # that overrides the config or just use a small hack.
    # Actually, let's just use a command line arg if we can, but hfdown doesn't have one for buffer size yet.
    # I'll quickly add a --buffer-size flag to main.cpp for this benchmark.
    
    if os.path.exists("benchmark_out"):
        subprocess.run(["rm", "-rf", "benchmark_out"])
    os.makedirs("benchmark_out")

    cmd = [
        "/usr/bin/time", "-l",
        "./build/hfdown", "download", "test/model", "benchmark_out",
        "--mirror", mirror_url,
        "--buffer-size", str(buffer_size_kb) # We'll add this flag
    ]
    
    start_time = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    end_time = time.time()
    
    duration = end_time - start_time
    print(f"Finished in {duration:.2f}s")
    
    # Parse time output
    stderr = result.stderr
    sys_time = 0
    user_time = 0
    for line in stderr.split('\n'):
        if 'real' in line and 'user' in line and 'sys' in line:
            parts = line.split()
            user_time = float(parts[2])
            sys_time = float(parts[4])
            break
    
    cpu_usage = ((user_time + sys_time) / duration) * 100
    print(f"CPU Usage: {cpu_usage:.2f}% (User: {user_time}s, Sys: {sys_time}s)")
    return cpu_usage

def main():
    setup_benchmark()
    
    print("Starting C++23 H2 server...")
    server_process = subprocess.Popen(["./h2_server", "8888"], 
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    time.sleep(1)
    mirror_url = "http://localhost:8888"

    results = {}
    # Test different buffer sizes
    for size in [4096, 8192, 16384, 32768, 65536]:
        try:
            usage = run_test(size, mirror_url)
            results[size] = usage
        except Exception as e:
            print(f"Test failed for {size}KB: {e}")

    server_process.terminate()
    
    print("\n=== Final Benchmark Results ===")
    print(f"{ 'Buffer Size (KB)':<20} | { 'CPU Usage (%)':<15}")
    print("-" * 40)
    for size, usage in sorted(results.items()):
        print(f"{size:<20} | {usage:<15.2f}")

if __name__ == "__main__":
    main()
