# hfdown Optimization & Stability Roadmap (64 Tasks)

## Link Speed & Connectivity

1. [ ] Implement multi-provider link latency auto-tester (HF vs. Kaggle vs. Mirrors).
2. [ ] Add support for `hf-mirror.com` with automatic failover if primary HF is slow.
3. [ ] Integrate DNS-over-HTTPS (DoH) via `libcurl` to prevent DNS poisoning and speed up resolution.
4. [ ] Implement Happy Eyeballs (RFC 8305) for simultaneous IPv4/IPv6 connection attempts.
5. [ ] Add `TCP_FASTOPEN` support to reduce latency for repeated HTTP/2 connections.
6. [ ] Implement a persistent session cache for QUIC (zero-RTT resumes).
7. [ ] Add support for custom SOCKS5/HTTP proxies via environment variables.
8. [ ] Implement "Dynamic Mirror Selection" based on the user's IP geolocation.
9. [ ] Add a `ping` command to test connectivity to all supported endpoints.
10. [ ] Optimize BBR congestion control settings via `setsockopt` where available on Linux.
11. [ ] Implement automatic retry logic with exponential backoff for 5xx errors.
12. [ ] Add support for `X-Forwarded-For` headers for complex network topologies.
13. [ ] Implement connection-level bandwidth throttling to prevent saturating small links.
14. [ ] Add support for "Resume-Only" mode to strictly avoid redownloading partial files.
15. [ ] Implement a "Network Health Monitor" that pauses downloads if packet loss exceeds 5%.
16. [ ] Add support for HTTP/2 Server Push for model metadata.

## Linux Kernel Efficiency

1. [ ] Integrate `io_uring` for asynchronous file writes to reduce system call overhead.
2. [ ] Enable UDP GSO (Generic Segment Offload) for 10x faster HTTP/3 packet sending.
3. [ ] Use `fallocate()` to pre-allocate disk space and prevent fragmentation.
4. [ ] Implement `splice()` based data transfer from socket to file to avoid user-space copying.
5. [ ] Optimize `TCP_NOTSENT_LOWAT` to minimize kernel memory pressure.
6. [ ] Implement `O_DIRECT` support for bypassing page cache on 100GB+ file downloads.
7. [ ] Add NUMA-aware thread pinning for high-end server environments.
8. [ ] Implement a custom `SO_REUSEPORT` load-balanced UDP socket pool.
9. [ ] Use `recvmmsg()` to batch receive multiple UDP packets in one kernel entry.
10. [ ] Implement CPU affinity masks for the main event loop vs worker threads.
11. [ ] Add support for Linux Control Groups (cgroups) for resource limiting.
12. [ ] Optimize large page support (HugePages) for internal buffer management.
13. [ ] Implement a "Quiet Mode" that reduces log-driven context switches.
14. [ ] Use `sendfile()` where possible for HTTP/1.1 mirror synchronization.
15. [ ] Implement a "Zero-Copy" path for `libcurl` write callbacks.
16. [ ] Add kernel-level packet filtering (BPF) for QUIC connection tracking.

## Memory & Space Optimization

1. [ ] Implement a pre-allocated Buffer Pool to eliminate `malloc` calls in the hot path.
2. [ ] Use `mmap` for reading large local files during rsync/checksum operations.
3. [ ] Implement Zstd dictionary pre-loading for ultra-fast JSON metadata parsing.
4. [ ] Add a "Low Memory Mode" that reduces parallel workers and buffer sizes.
5. [ ] Implement periodic `malloc_trim` to return unused memory to the OS.
6. [ ] Use bit-fields and compact structs to reduce cache-misses in metadata storage.
7. [ ] Implement a "Streaming Checksum" state machine to handle resumed downloads.
8. [ ] Add support for Sparse Files to save space on models with large empty sections.
9. [ ] Optimize JSON parsing by using a SAX-style pull parser instead of DOM.
10. [ ] Implement a LRU cache for model file metadata.
11. [ ] Use `string_view` throughout the URL and header parsing logic.
12. [ ] Implement inter-thread message passing without allocations (Lock-free queues).
13. [ ] Add support for shared memory IPC if running multiple instances.
14. [ ] Implement automatic garbage collection of partial `.incomplete` files.
15. [ ] Add a "Compress-on-Disk" option for text-heavy datasets.
16. [ ] Optimize the `alt-svc` cache storage using a binary format.

## Stability & Robustness

1. [ ] Implement a "Heartbeat" mechanism for long-lived QUIC streams.
2. [ ] Add comprehensive "Panic" handling to safely close file handles on crashes.
3. [ ] Implement a "Corruption Recovery" tool that re-downloads only damaged chunks.
4. [ ] Add support for hardware-accelerated SHA256 using Intel SHA-NI or ARM Crypto.
5. [ ] Implement a "Dry-Run Checksum" to verify local files against remote OIDs.
6. [ ] Add detailed signal handling (SIGINT/SIGTERM) for clean shutdowns.
7. [ ] Implement a "Safe Mode" that disables all experimental kernel optimizations.
8. [ ] Add support for Hugging Face "Gated Models" with automatic auth prompts.
9. [ ] Implement a "Disk Space Watchdog" that pauses downloads before disk fill.
10. [ ] Add comprehensive unit tests for the HTTP/3 handshake state machine.
11. [ ] Implement a "Protocol Fallback Log" to diagnose why H3 might be failing.
12. [ ] Add support for custom Certificate Authorities (CA) for corporate firewalls.
13. [ ] Implement a "Dependency Checker" to verify OpenSSL and libcurl versions.
14. [ ] Add a "Fuzzing" harness for the custom protocol parsing logic.
15. [ ] Implement a "Version Update Notifier" for hfdown itself.
16. [ ] Add a "Performance Summary" report after every large download.

## Predictive & Size-Aware Optimizations

1. [ ] Implement "Large File First" scheduling to minimize the tail-latency of downloads.
2. [ ] Add `posix_fallocate` for all files to ensure contiguous disk layout and zero fragmentation.
3. [ ] Implement "Dynamic Chunking": split files >1GB into parts for even more parallelism.
4. [ ] Add "Small File Coalescing": fetch files <10KB using a single multiplexed H2/H3 stream.
5. [ ] Implement "Zero-Fill Detection": avoid downloading blocks that are known to be empty.
6. [x] Add "Predictive Buffer Scaling": use 8MB buffers for large files and 16KB for metadata.
7. [ ] Implement "Global Progress Weighting": calculate progress by bytes, not file count.
8. [ ] Add "Disk-Aware Parallelism": limit concurrent writes based on disk IOPS (SSD vs HDD).
9. [ ] Implement "Memory-Mapped Verification": OID check files via `mmap` if they fit in RAM.
10. [ ] Add "Network Path Diversity": use different CDN IP addresses for the top 3 largest files.
11. [ ] Implement "Ahead-of-Time Directory Sync": create all folders before any data transfer starts.
12. [ ] Add "Pre-Download Integrity Check": verify local disk space against total model size + 10%.
13. [ ] Implement "Batched Mirror Selection": test mirror speeds using the smallest 5 files in the model.
14. [ ] Add "Write-Through Cache Bypass": use `O_DIRECT` for files that exceed 50% of system RAM.
15. [ ] Implement "Simultaneous Protocol Probing": start H3 for large files while H2 handles small ones.
16. [ ] Add "Worker Thread Stealing": allows threads finishing small files to help with large multi-part files.

## Modernization & Refactoring

81.- [x] Migrate to Meson build system <!-- id: 71 -->

- [x] Create meson.build <!-- id: 72 -->
- [x] Replace libcurl with custom implementation <!-- id: 73 -->
HTTP/1.1+TLS implementation.
