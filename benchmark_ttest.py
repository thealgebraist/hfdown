import subprocess
import time
import os
import shutil
import numpy as np
from scipy import stats
import argparse

def run_hfdown(binary, model_id, output_dir, mirror, threads=None):
    cmd = [binary, "download", model_id, output_dir, "--mirror", mirror]
    if threads:
        cmd.extend(["--threads", str(threads)])
    
    start = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    end = time.time()
    
    if result.returncode != 0:
        print(f"Error running {binary}:")
        print(f"STDOUT: {result.stdout}")
        print(f"STDERR: {result.stderr}")
        return None
    return end - start

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="./build/hfdown")
    parser.add_argument("--model-id", default="test/random-model")
    parser.add_argument("--mirror", default="http://localhost:8891")
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--compare-threads", type=int, nargs=2, default=[1, 4])
    args = parser.parse_args()

    results = {}

    for threads in args.compare_threads:
        print(f"\nBenchmarking {args.binary} with {threads} threads...")
        
        # Warmup
        print("  Warmup run...")
        run_hfdown(args.binary, args.model_id, "test_warmup", args.mirror, threads=threads)
        if os.path.exists("test_warmup"):
            shutil.rmtree("test_warmup")

        times = []
        for i in range(args.iterations):
            output_dir = f"test_download_t{threads}_{i}"
            if os.path.exists(output_dir):
                shutil.rmtree(output_dir)
            
            t = run_hfdown(args.binary, args.model_id, output_dir, args.mirror, threads=threads)
            if t is not None:
                times.append(t)
                print(f"  Trial {i+1}: {t:.3f}s")
            
            if os.path.exists(output_dir):
                shutil.rmtree(output_dir)
        
        results[threads] = times

    # Statistical Analysis
    t1, t2 = args.compare_threads
    times1 = results[t1]
    times2 = results[t2]

    if not times1 or not times2:
        print("Benchmarking failed for one or more configurations.")
        return

    m1, s1 = np.mean(times1), np.std(times1)
    m2, s2 = np.mean(times2), np.std(times2)

    print(f"\nResults Summary:")
    print(f"Threads {t1}: Mean = {m1:.3f}s, StdDev = {s1:.3f}s")
    print(f"Threads {t2}: Mean = {m2:.3f}s, StdDev = {s2:.3f}s")

    t_stat, p_val = stats.ttest_ind(times1, times2)
    print(f"\nT-Test Comparison ({t1} threads vs {t2} threads):")
    print(f"T-statistic: {t_stat:.4f}")
    print(f"P-value: {p_val:.4f}")

    if p_val < 0.05:
        improvement = (m1 - m2) / m1 * 100
        print(f"Status: Statistically significant difference found!")
        if m1 > m2:
            print(f"Improvement: {improvement:.1f}% faster with {t2} threads.")
        else:
            print(f"Degradation: {(-improvement):.1f}% slower with {t2} threads.")
    else:
        print("Status: No statistically significant difference found.")

if __name__ == "__main__":
    main()
