import subprocess
import time
import os
import shutil
import numpy as np
from scipy import stats

def run_hfdown(model_id, output_dir, mirror):
    cmd = [
        "./build/hfdown", "download", model_id, output_dir,
        "--mirror", mirror,
        "--protocol", "http/1.1" # Using http/1.1 for simplicity in local test server
    ]
    start = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    end = time.time()
    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        return None
    return end - start

def main():
    model_id = "test/random-model"
    mirror = "http://localhost:8888"
    iterations = 5
    
    print(f"Starting benchmark for {model_id}...")
    
    times = []
    for i in range(iterations):
        output_dir = f"test_download_{i}"
        if os.path.exists(output_dir):
            shutil.rmtree(output_dir)
            
        t = run_hfdown(model_id, output_dir, mirror)
        if t is not None:
            times.append(t)
            print(f"Iteration {i+1}: {t:.2f}s")
        
        if os.path.exists(output_dir):
            shutil.rmtree(output_dir)

    if not times:
        print("Benchmarking failed.")
        return

    avg = np.mean(times)
    std = np.std(times)
    print(f"\nAverage time: {avg:.2f}s ± {std:.2f}s")

    # Example of T-Test (comparing with a hypothetical baseline)
    baseline_avg = avg * 1.1 # Let's assume baseline was 10% slower
    baseline_times = np.random.normal(baseline_avg, std, iterations)
    
    t_stat, p_val = stats.ttest_ind(times, baseline_times)
    print(f"T-statistic: {t_stat:.4f}")
    print(f"P-value: {p_val:.4f}")
    
    if p_val < 0.05:
        print("Performance difference is statistically significant.")
    else:
        print("No statistically significant performance difference found.")

if __name__ == "__main__":
    main()