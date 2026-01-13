# Vast.ai Resource Monitoring Feature

## Overview

This feature adds comprehensive GPU and CPU resource monitoring for Vast.ai servers with visualization capabilities.

## What Was Added

### New Files

1. **`include/vast_monitor.hpp`** - Header file defining:
   - `GpuMetrics` struct - GPU utilization, memory, temperature, power
   - `CpuMetrics` struct - CPU utilization, memory, load averages
   - `SystemMetrics` struct - Combined metrics with timestamp
   - `MonitorConfig` struct - Configuration options
   - `VastMonitor` class - Main monitoring implementation

2. **`src/vast_monitor.cpp`** - Implementation providing:
   - SSH command execution to remote servers
   - nvidia-smi parsing for GPU metrics
   - System command parsing for CPU metrics
   - CSV file writing with headers
   - Real-time console display
   - Configurable monitoring loops

3. **`visualize_monitor.py`** - Python visualization script:
   - Parses CSV monitoring data
   - Generates 6 comprehensive graphs
   - Provides summary statistics
   - Supports single or multiple GPUs
   - Professional chart formatting

### Modified Files

1. **`src/main.cpp`** - Added:
   - `vast-monitor` command handler
   - Command-line option parsing (--interval, --duration, --output)
   - Help text and examples

2. **`CMakeLists.txt`** - Added:
   - `src/vast_monitor.cpp` to build targets

3. **`README.md`** - Added:
   - Complete usage documentation
   - Examples and workflow
   - Requirements and installation
   - Sample output

4. **`.gitignore`** - Added:
   - Generated CSV files
   - Generated PNG images

## Usage Examples

### Basic Monitoring

```bash
# Monitor for 60 seconds (default)
./hfdown vast-monitor 'ssh -p 12345 root@1.2.3.4'
```

### Custom Configuration

```bash
# Monitor every 10 seconds for 5 minutes
./hfdown vast-monitor 'ssh -p 12345 root@1.2.3.4' \
  --interval 10 \
  --duration 300 \
  --output my_metrics.csv
```

### Continuous Monitoring

```bash
# Monitor until manually stopped (Ctrl+C)
./hfdown vast-monitor 'ssh -p 12345 root@1.2.3.4' --duration 0
```

### Generate Visualization

```bash
# Create graphs from collected data
./visualize_monitor.py vast_monitor.csv
# Output: vast_monitor.png with 6 graphs
```

## Collected Metrics

### GPU Metrics (per GPU)
- Utilization percentage
- Memory used/total (MB)
- Temperature (°C)
- Power draw/limit (W)

### CPU/System Metrics
- CPU utilization percentage
- System memory used/total (MB)
- Load averages (1min, 5min, 15min)

## Output Format

### CSV File
Time-series data with columns:
- timestamp
- gpu_id, gpu_name, gpu_util_%, gpu_mem_used_mb, gpu_mem_total_mb, gpu_temp_c, gpu_power_w, gpu_power_limit_w
- cpu_util_%, mem_used_mb, mem_total_mb, load_1min, load_5min, load_15min

### Visualization
6 graphs showing:
1. GPU Utilization over time
2. GPU Memory Usage over time
3. GPU Temperature over time
4. GPU Power Consumption over time
5. CPU Utilization over time
6. System Memory Usage over time

### Console Output
Real-time display of current metrics with formatting:
```
======================================================================
Metrics at: 2024-01-13 10:00:00
======================================================================

GPU Metrics:
  GPU 0: NVIDIA RTX 4090
    Utilization:  78.0%
    Memory:       18000/24000 MB (75.0%)
    Temperature:  72.0°C
    Power:        350.0/450.0 W

CPU/System Metrics:
  CPU Utilization:  65.0%
  Memory:           12000/32000 MB (37.5%)
  Load Average:     1.80, 1.60, 1.40
======================================================================
```

## Implementation Details

### Architecture
- Uses existing SSH infrastructure from `rsync_client`
- Executes remote commands: `nvidia-smi`, `top`, `free`, `uptime`
- Parses CSV output from nvidia-smi
- Parses text output from system commands
- Thread-safe with configurable intervals

### Error Handling
- SSH connection failures
- Remote command failures
- Parse errors
- File system errors
- Graceful degradation (continues if GPU not available)

### Integration
- Follows existing CLI patterns
- Uses std::expected for error handling
- Compatible with C++23 features
- Minimal dependencies (uses existing OpenSSL for nothing extra)

## Testing

Tested with:
- Sample CSV data generation
- Visualization script validation
- Compilation verification
- Help system integration
- Error handling (missing SSH command)

## Future Enhancements

Potential improvements:
- [ ] Support for multiple simultaneous monitoring sessions
- [ ] Web dashboard for real-time viewing
- [ ] Alert thresholds (email/notification on high temp, etc.)
- [ ] Historical data comparison
- [ ] Export to other formats (JSON, SQLite)
- [ ] Network bandwidth monitoring
- [ ] Disk I/O monitoring
