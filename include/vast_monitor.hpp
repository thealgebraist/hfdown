#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <expected>
#include <filesystem>

namespace hfdown {

struct GpuMetrics {
    int gpu_id = 0;
    std::string gpu_name;
    float utilization_percent = 0.0f;      // GPU utilization %
    size_t memory_used_mb = 0;             // GPU memory used (MB)
    size_t memory_total_mb = 0;            // GPU memory total (MB)
    float temperature_celsius = 0.0f;      // GPU temperature (C)
    float power_draw_watts = 0.0f;         // Power draw (W)
    float power_limit_watts = 0.0f;        // Power limit (W)
};

struct CpuMetrics {
    float utilization_percent = 0.0f;      // Overall CPU utilization %
    size_t memory_used_mb = 0;             // System RAM used (MB)
    size_t memory_total_mb = 0;            // System RAM total (MB)
    float load_average_1min = 0.0f;        // 1-min load average
    float load_average_5min = 0.0f;        // 5-min load average
    float load_average_15min = 0.0f;       // 15-min load average
};

struct SystemMetrics {
    std::chrono::system_clock::time_point timestamp;
    std::vector<GpuMetrics> gpus;
    CpuMetrics cpu;
};

enum class VastMonitorError {
    SshConnectionFailed,
    RemoteCommandFailed,
    ParseError,
    FileSystemError,
    NoGpuFound
};

struct VastMonitorErrorInfo {
    VastMonitorError error;
    std::string message;
};

struct MonitorConfig {
    std::string ssh_command;               // SSH command (e.g., "ssh -p 12345 root@1.2.3.4")
    int interval_seconds = 5;              // Sampling interval in seconds
    int duration_seconds = 60;             // Total monitoring duration (0 = infinite)
    std::filesystem::path output_file;     // CSV output file path
    bool show_realtime = true;             // Show real-time output to console
    bool include_cpu = true;               // Include CPU metrics
    bool include_gpu = true;               // Include GPU metrics
};

// Monitor resource usage on a remote Vast.ai server
class VastMonitor {
public:
    VastMonitor() = default;
    
    // Start monitoring with given configuration
    std::expected<void, VastMonitorErrorInfo> start_monitoring(const MonitorConfig& config);
    
    // Get current metrics snapshot (single sample)
    std::expected<SystemMetrics, VastMonitorErrorInfo> get_metrics(const std::string& ssh_command);
    
private:
    // Execute SSH command and return output
    std::expected<std::string, VastMonitorErrorInfo> ssh_execute(
        const std::string& ssh_command,
        const std::string& remote_command
    );
    
    // Parse nvidia-smi output to extract GPU metrics
    std::expected<std::vector<GpuMetrics>, VastMonitorErrorInfo> parse_gpu_metrics(
        const std::string& nvidia_smi_output
    );
    
    // Parse system metrics (CPU, RAM, load)
    std::expected<CpuMetrics, VastMonitorErrorInfo> parse_cpu_metrics(
        const std::string& top_output,
        const std::string& free_output,
        const std::string& uptime_output
    );
    
    // Write metrics to CSV file
    std::expected<void, VastMonitorErrorInfo> write_csv_header(
        const std::filesystem::path& output_file,
        bool include_gpu,
        bool include_cpu
    );
    
    std::expected<void, VastMonitorErrorInfo> append_metrics_to_csv(
        const std::filesystem::path& output_file,
        const SystemMetrics& metrics
    );
    
    // Display metrics in console
    void display_metrics(const SystemMetrics& metrics);
};

} // namespace hfdown
