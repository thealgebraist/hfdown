#!/usr/bin/env python3
import math

# =================================================================================
# System Constants (High-End Workstation / Modern Mac Optimization)
# =================================================================================

# Refined Constants
# 1. TCP / Network Realism
RTT_MS = 100.0  # 100ms Round Trip Time (intercontinental)
TCP_WINDOW_SCALE = 1.0  # Effective Window scaling factor efficiency
PACKET_LOSS = 0.0001  # 0.01% packet loss

# 1. Hardware Limits
CPU_CORES = 8  # Performance cores
CPU_FREQ = 3.2e9  # 3.2 GHz
RAM_SIZE_GB = 16  # Total System RAM
RAM_BW_GBPS = 100.0  # 100 GB/s (M1/M2/M3 Pro/Max class)
DISK_BW_GBPS = 4.0  # 4 GB/s NVMe Sequential Write
NET_BW_GBPS = 10.0  # 10 Gbps Link (Targeting high speed)

# 2. Latencies & Overhead (Cycles)
# Benchmarked/Estimated values for modern Unix-like kernel (macOS/Linux)
CYCLES_SYSCALL_BASE = 1500  # Cost of entering/exiting kernel (read/write)
CYCLES_CTX_SWITCH = 3000  # Determining next thread
CYCLES_K_FRAMEWORK = 500  # Overhead of internal kernel buffer management

# 3. Processing Costs (per byte) - Revised
# TLS overhead includes record layer framing, MAC check, IV generation.
# Real-world observed OpenSSL throughput on single core is often ~1-2 GB/s, not 5+
CYCLES_TLS_PER_BYTE = 12.0  # Revised up from 5.0 to reflect protocol overhead
CYCLES_MEMCPY_PER_BYTE = 0.5  # Single memcpy (SIMD optimized)
CYCLES_HTTP_PARSE = 1.0  # Checksum/boundary checks

# 4. Memory/Cache Hierarchy impact
# If buffer fits in L2, latency is lower, but for streaming downloads
# we dominate L2/L3 with new data anyway.
CACHE_L2_SIZE = 12 * 1024 * 1024

# =================================================================================
# Optimization Model
# =================================================================================
#
# Objective: Maximize R (Throughput in bytes/sec)
#
# Variables:
#   N (int): Number of threads [1..64]
#   B (int): Buffer size (bytes) [4KB .. 128MB]
#
# Constraints:
# 1. Network Limit: R <= NET_BW
# 2. Disk Limit:    R <= DISK_BW
# 5. Memory BW:     R * (Reads+Writes) <= RAM_BW
#    (Download flow: NIC->Kernel(W) -> User(R) -> User(W) -> Kernel(R) -> Disk(W))
#    Worst case ~3-4 passes over RAM. Let's say 4.
#    R * 4 <= RAM_BW
#
# 3. CPU Constraint:
#    Total Cycles Available = N_CORES * CPU_FREQ
#    Cycles Required = R * Cost_Per_Byte + (R / B) * Cost_Per_Chunk
#
#    Cost_Per_Byte = CYCLES_TLS + CYCLES_MEMCPY + CYCLES_HTTP_PARSE
#    Cost_Per_Chunk = (CYCLES_SYSCALL_BASE + CYCLES_K_FRAMEWORK) * 2 (Read+Write)
#                     + CYCLES_CTX_SWITCH (amortized)
#
# 4. Memory Capacity:
#    N * B <= SAFE_RAM_LIMIT (e.g., 2GB or 10% of RAM)


def solve_optimization():
    # Derived Constants
    MAX_RAM_USAGE = 4.0 * 1024**3  # 4GB

    # Costs
    cost_per_byte = CYCLES_TLS_PER_BYTE + CYCLES_MEMCPY_PER_BYTE + CYCLES_HTTP_PARSE
    cost_per_chunk = (CYCLES_SYSCALL_BASE + CYCLES_K_FRAMEWORK) * 2 + CYCLES_CTX_SWITCH

    # Bandwidth Limits (in Bytes/sec)
    limit_net_hw = NET_BW_GBPS * 1e9 / 8.0
    limit_disk = DISK_BW_GBPS * 1e9
    limit_ram_bw = (RAM_BW_GBPS * 1e9) / 4.0  # Effective throughput limit due to copies

    # TCP Throughput Limit (Mathis Equation approximation or BPD limit)
    # Single stream limit ~= WindowSize / RTT
    # Modern TCP stack auto-tunes window, but realistic single-stream max is often ~1-2 Gbps over WAN due to loss/latency
    # BDP = Bandwidth * Delay
    # Max practical single thread speed often capped by receiver window or congestion control dynamics
    limit_tcp_per_thread = (
        2.0 * 1024**3
    ) / 8.0  # Cap single thread at 2Gbps realistic WAN

    global_limit_hw = min(limit_net_hw, limit_disk, limit_ram_bw)

    print(f"--- Optimization Inputs (Refined) ---")
    print(f"CPU: {CPU_CORES} cores @ {CPU_FREQ / 1e9:.1f} GHz")
    print(f"Network HW Limit: {limit_net_hw / 1e6:.1f} MB/s")
    print(f"Global HW Limit: {global_limit_hw / 1e6:.1f} MB/s")
    print(f"Cost per Byte: {cost_per_byte} cycles")
    print(f"Cost per Chunk: {cost_per_chunk} cycles (syscalls + overhead)")
    print(f"TCP RTT: {RTT_MS} ms")
    print(f"---------------------------\n")

    def run_scenario(cpu_limit_percent, duration_str):
        print(
            f"=== Scenario: {cpu_limit_percent * 100:.0f}% CPU Limit ({duration_str}) ==="
        )

        best_r = 0
        best_n = 1
        best_b = 4096

        # Grid Search
        buffers = [64 * 1024 * (2**i) for i in range(12)]  # 64KB -> 128MB
        threads = range(1, 65)

        for n in threads:
            for b in buffers:
                if n * b > MAX_RAM_USAGE:
                    continue

                limit_net_effective = min(limit_net_hw, n * limit_tcp_per_thread)

                denom = cost_per_byte + (cost_per_chunk / b)
                thread_overhead = n * 5000
                total_cycles_avail = (
                    CPU_CORES * CPU_FREQ * cpu_limit_percent
                ) - thread_overhead

                if total_cycles_avail <= 0:
                    continue

                # Max throughput CPU can sustain at this limit
                max_r_cpu = total_cycles_avail / denom

                # Intersection
                r = min(limit_disk, limit_ram_bw, limit_net_effective, max_r_cpu)

                if r > best_r:
                    best_r = r
                    best_n = n
                    best_b = b

        print(f"Threads:       {best_n}")
        print(f"Buffer Size:   {best_b / 1024 / 1024:.2f} MB")
        print(
            f"Throughput:    {best_r / 1024 / 1024:.2f} MB/s ({best_r * 8 / 1e9:.2f} Gbps)"
        )

        # CPU Check
        denom = cost_per_byte + (cost_per_chunk / best_b)
        thread_overhead = best_n * 5000
        cycles_used = (best_r * denom) + thread_overhead
        actual_cpu = cycles_used / (CPU_CORES * CPU_FREQ)
        print(f"Actual CPU:    {actual_cpu * 100:.1f}%")
        print(f"Network Sat:   {best_r / limit_net_hw * 100:.1f}%")
        print(
            f"Limiting Factor: {'CPU' if best_r >= 0.99 * ((CPU_CORES * CPU_FREQ * cpu_limit_percent - thread_overhead) / denom) else 'Network/Disk'}"
        )
        print("")
        return best_r

    r_low = run_scenario(0.10, "5 mins")
    r_high = run_scenario(0.90, "20 sec")

    # Total Data Analysis
    total_data = (r_low * 300) + (r_high * 20)
    print(f"--- Session Total ---")
    print(f"Total Data Transferred: {total_data / 1024**3:.2f} GB")
    print(f"Avg Throughput: {total_data / 320 / 1024 / 1024:.2f} MB/s")


if __name__ == "__main__":
    solve_optimization()
