#include "vast_monitor.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <format>
#include <regex>
#include <thread>
#include <iomanip>
#include <cstdio>
#include <ctime>

namespace hfdown {

std::expected<std::string, VastMonitorErrorInfo> VastMonitor::ssh_execute(
    const std::string& ssh_command,
    const std::string& remote_command)
{
    // Build full SSH command
    std::string full_cmd = ssh_command + " -o StrictHostKeyChecking=no -o BatchMode=yes \"" + remote_command + "\"";
    
    // Execute command and capture output
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        return std::unexpected(VastMonitorErrorInfo{
            VastMonitorError::SshConnectionFailed,
            "Failed to execute SSH command"
        });
    }
    
    std::ostringstream output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output << buffer;
    }
    
    int status = pclose(pipe);
    if (status != 0) {
        return std::unexpected(VastMonitorErrorInfo{
            VastMonitorError::RemoteCommandFailed,
            std::format("Remote command failed with exit code: {}", status)
        });
    }
    
    return output.str();
}

std::expected<std::vector<GpuMetrics>, VastMonitorErrorInfo> VastMonitor::parse_gpu_metrics(
    const std::string& nvidia_smi_output)
{
    std::vector<GpuMetrics> gpus;
    
    // Parse CSV output from nvidia-smi
    // Expected format: index, name, utilization.gpu, memory.used, memory.total, temperature.gpu, power.draw, power.limit
    std::istringstream stream(nvidia_smi_output);
    std::string line;
    
    // Skip header line
    std::getline(stream, line);
    
    while (std::getline(stream, line)) {
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        
        GpuMetrics gpu;
        std::istringstream linestream(line);
        std::string field;
        
        try {
            // Parse CSV fields
            std::getline(linestream, field, ',');
            gpu.gpu_id = std::stoi(field);
            
            std::getline(linestream, field, ',');
            gpu.gpu_name = field;
            // Trim whitespace
            gpu.gpu_name.erase(0, gpu.gpu_name.find_first_not_of(" \t"));
            gpu.gpu_name.erase(gpu.gpu_name.find_last_not_of(" \t") + 1);
            
            std::getline(linestream, field, ',');
            gpu.utilization_percent = std::stof(field);
            
            std::getline(linestream, field, ',');
            gpu.memory_used_mb = std::stoull(field);
            
            std::getline(linestream, field, ',');
            gpu.memory_total_mb = std::stoull(field);
            
            std::getline(linestream, field, ',');
            gpu.temperature_celsius = std::stof(field);
            
            std::getline(linestream, field, ',');
            gpu.power_draw_watts = std::stof(field);
            
            std::getline(linestream, field, ',');
            gpu.power_limit_watts = std::stof(field);
            
            gpus.push_back(gpu);
        } catch (const std::exception& e) {
            return std::unexpected(VastMonitorErrorInfo{
                VastMonitorError::ParseError,
                std::format("Failed to parse GPU metrics: {}", e.what())
            });
        }
    }
    
    if (gpus.empty()) {
        return std::unexpected(VastMonitorErrorInfo{
            VastMonitorError::NoGpuFound,
            "No GPU found in nvidia-smi output"
        });
    }
    
    return gpus;
}

std::expected<CpuMetrics, VastMonitorErrorInfo> VastMonitor::parse_cpu_metrics(
    const std::string& top_output,
    const std::string& free_output,
    const std::string& uptime_output)
{
    CpuMetrics cpu;
    
    try {
        // Parse CPU utilization from top (looking for "Cpu(s)" line)
        std::regex cpu_regex(R"(Cpu\(s\):\s+[\d.]+\s+us,\s+[\d.]+\s+sy,\s+[\d.]+\s+ni,\s+([\d.]+)\s+id)");
        std::smatch match;
        if (std::regex_search(top_output, match, cpu_regex)) {
            float idle_percent = std::stof(match[1]);
            cpu.utilization_percent = 100.0f - idle_percent;
        }
        
        // Parse memory from free (looking for "Mem:" line)
        std::regex mem_regex(R"(Mem:\s+(\d+)\s+(\d+))");
        if (std::regex_search(free_output, match, mem_regex)) {
            cpu.memory_total_mb = std::stoull(match[1]) / 1024; // Convert KB to MB
            cpu.memory_used_mb = std::stoull(match[2]) / 1024;
        }
        
        // Parse load average from uptime
        std::regex load_regex(R"(load average:\s+([\d.]+),\s+([\d.]+),\s+([\d.]+))");
        if (std::regex_search(uptime_output, match, load_regex)) {
            cpu.load_average_1min = std::stof(match[1]);
            cpu.load_average_5min = std::stof(match[2]);
            cpu.load_average_15min = std::stof(match[3]);
        }
    } catch (const std::exception& e) {
        return std::unexpected(VastMonitorErrorInfo{
            VastMonitorError::ParseError,
            std::format("Failed to parse CPU metrics: {}", e.what())
        });
    }
    
    return cpu;
}

std::expected<SystemMetrics, VastMonitorErrorInfo> VastMonitor::get_metrics(
    const std::string& ssh_command)
{
    SystemMetrics metrics;
    metrics.timestamp = std::chrono::system_clock::now();
    
    // Get GPU metrics
    auto gpu_cmd_result = ssh_execute(ssh_command,
        "nvidia-smi --query-gpu=index,name,utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw,power.limit "
        "--format=csv,noheader,nounits");
    
    if (gpu_cmd_result) {
        auto gpu_metrics = parse_gpu_metrics(*gpu_cmd_result);
        if (gpu_metrics) {
            metrics.gpus = std::move(*gpu_metrics);
        }
        // Continue even if GPU parsing fails (might be CPU-only instance)
    }
    
    // Get CPU metrics
    auto top_result = ssh_execute(ssh_command, "top -bn1 | head -5");
    auto free_result = ssh_execute(ssh_command, "free -k | grep Mem");
    auto uptime_result = ssh_execute(ssh_command, "uptime");
    
    if (top_result && free_result && uptime_result) {
        auto cpu_metrics = parse_cpu_metrics(*top_result, *free_result, *uptime_result);
        if (cpu_metrics) {
            metrics.cpu = *cpu_metrics;
        } else {
            return std::unexpected(cpu_metrics.error());
        }
    } else {
        return std::unexpected(VastMonitorErrorInfo{
            VastMonitorError::RemoteCommandFailed,
            "Failed to get CPU metrics"
        });
    }
    
    return metrics;
}

std::expected<void, VastMonitorErrorInfo> VastMonitor::write_csv_header(
    const std::filesystem::path& output_file,
    bool include_gpu,
    bool include_cpu)
{
    std::ofstream file(output_file);
    if (!file) {
        return std::unexpected(VastMonitorErrorInfo{
            VastMonitorError::FileSystemError,
            std::format("Failed to open output file: {}", output_file.string())
        });
    }
    
    // Write CSV header
    file << "timestamp";
    
    if (include_gpu) {
        file << ",gpu_id,gpu_name,gpu_util_%,gpu_mem_used_mb,gpu_mem_total_mb,"
             << "gpu_temp_c,gpu_power_w,gpu_power_limit_w";
    }
    
    if (include_cpu) {
        file << ",cpu_util_%,mem_used_mb,mem_total_mb,"
             << "load_1min,load_5min,load_15min";
    }
    
    file << "\n";
    return {};
}

std::expected<void, VastMonitorErrorInfo> VastMonitor::append_metrics_to_csv(
    const std::filesystem::path& output_file,
    const SystemMetrics& metrics)
{
    std::ofstream file(output_file, std::ios::app);
    if (!file) {
        return std::unexpected(VastMonitorErrorInfo{
            VastMonitorError::FileSystemError,
            std::format("Failed to open output file: {}", output_file.string())
        });
    }
    
    // Format timestamp
    auto time_t = std::chrono::system_clock::to_time_t(metrics.timestamp);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));
    
    // Write GPU metrics (one row per GPU)
    if (!metrics.gpus.empty()) {
        for (const auto& gpu : metrics.gpus) {
            file << time_buf << ","
                 << gpu.gpu_id << ","
                 << gpu.gpu_name << ","
                 << gpu.utilization_percent << ","
                 << gpu.memory_used_mb << ","
                 << gpu.memory_total_mb << ","
                 << gpu.temperature_celsius << ","
                 << gpu.power_draw_watts << ","
                 << gpu.power_limit_watts << ","
                 << metrics.cpu.utilization_percent << ","
                 << metrics.cpu.memory_used_mb << ","
                 << metrics.cpu.memory_total_mb << ","
                 << metrics.cpu.load_average_1min << ","
                 << metrics.cpu.load_average_5min << ","
                 << metrics.cpu.load_average_15min << "\n";
        }
    } else {
        // CPU-only metrics (8 empty GPU fields: gpu_id,gpu_name,gpu_util_%,gpu_mem_used_mb,gpu_mem_total_mb,gpu_temp_c,gpu_power_w,gpu_power_limit_w)
        const char* empty_gpu_fields = ",,,,,,,,";
        file << time_buf << ","
             << empty_gpu_fields
             << metrics.cpu.utilization_percent << ","
             << metrics.cpu.memory_used_mb << ","
             << metrics.cpu.memory_total_mb << ","
             << metrics.cpu.load_average_1min << ","
             << metrics.cpu.load_average_5min << ","
             << metrics.cpu.load_average_15min << "\n";
    }
    
    return {};
}

void VastMonitor::display_metrics(const SystemMetrics& metrics) {
    // Format timestamp
    auto time_t = std::chrono::system_clock::to_time_t(metrics.timestamp);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));
    
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "Metrics at: " << time_buf << "\n";
    std::cout << std::string(70, '=') << "\n";
    
    // Display GPU metrics
    if (!metrics.gpus.empty()) {
        std::cout << "\nGPU Metrics:\n";
        for (const auto& gpu : metrics.gpus) {
            std::cout << std::format("  GPU {}: {}\n", gpu.gpu_id, gpu.gpu_name);
            std::cout << std::format("    Utilization:  {:.1f}%\n", gpu.utilization_percent);
            std::cout << std::format("    Memory:       {}/{} MB ({:.1f}%)\n",
                                    gpu.memory_used_mb, gpu.memory_total_mb,
                                    100.0f * gpu.memory_used_mb / gpu.memory_total_mb);
            std::cout << std::format("    Temperature:  {:.1f}°C\n", gpu.temperature_celsius);
            std::cout << std::format("    Power:        {:.1f}/{:.1f} W\n",
                                    gpu.power_draw_watts, gpu.power_limit_watts);
        }
    }
    
    // Display CPU metrics
    std::cout << "\nCPU/System Metrics:\n";
    std::cout << std::format("  CPU Utilization:  {:.1f}%\n", metrics.cpu.utilization_percent);
    std::cout << std::format("  Memory:           {}/{} MB ({:.1f}%)\n",
                            metrics.cpu.memory_used_mb, metrics.cpu.memory_total_mb,
                            100.0f * metrics.cpu.memory_used_mb / metrics.cpu.memory_total_mb);
    std::cout << std::format("  Load Average:     {:.2f}, {:.2f}, {:.2f}\n",
                            metrics.cpu.load_average_1min,
                            metrics.cpu.load_average_5min,
                            metrics.cpu.load_average_15min);
    
    std::cout << std::string(70, '=') << "\n";
}

std::expected<void, VastMonitorErrorInfo> VastMonitor::start_monitoring(
    const MonitorConfig& config)
{
    std::cout << std::format("Starting resource monitoring...\n");
    std::cout << std::format("  SSH Command:  {}\n", config.ssh_command);
    std::cout << std::format("  Interval:     {} seconds\n", config.interval_seconds);
    std::cout << std::format("  Duration:     {} seconds{}\n",
                            config.duration_seconds,
                            config.duration_seconds == 0 ? " (infinite)" : "");
    std::cout << std::format("  Output File:  {}\n", config.output_file.string());
    
    // Test SSH connection first
    std::cout << "\nTesting SSH connection...\n";
    auto test_result = ssh_execute(config.ssh_command, "echo 'Connection test successful'");
    if (!test_result) {
        return std::unexpected(test_result.error());
    }
    std::cout << "SSH connection OK\n";
    
    // Create CSV file and write header
    auto header_result = write_csv_header(config.output_file, config.include_gpu, config.include_cpu);
    if (!header_result) {
        return header_result;
    }
    
    std::cout << "\nMonitoring started. Press Ctrl+C to stop.\n";
    
    auto start_time = std::chrono::steady_clock::now();
    int sample_count = 0;
    
    while (true) {
        // Get metrics
        auto metrics_result = get_metrics(config.ssh_command);
        if (!metrics_result) {
            std::cerr << std::format("Warning: Failed to get metrics: {}\n",
                                    metrics_result.error().message);
        } else {
            const auto& metrics = *metrics_result;
            
            // Display to console if requested
            if (config.show_realtime) {
                display_metrics(metrics);
            }
            
            // Write to CSV
            auto write_result = append_metrics_to_csv(config.output_file, metrics);
            if (!write_result) {
                std::cerr << std::format("Warning: Failed to write to CSV: {}\n",
                                        write_result.error().message);
            }
            
            sample_count++;
        }
        
        // Check if duration limit reached
        if (config.duration_seconds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= config.duration_seconds) {
                break;
            }
        }
        
        // Sleep for interval
        std::this_thread::sleep_for(std::chrono::seconds(config.interval_seconds));
    }
    
    std::cout << std::format("\nMonitoring completed. {} samples collected.\n", sample_count);
    std::cout << std::format("Data saved to: {}\n", config.output_file.string());
    
    return {};
}

} // namespace hfdown
