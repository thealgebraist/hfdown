#!/usr/bin/env python3
"""
Visualize Vast.ai resource monitoring data collected by hfdown

Usage:
    python3 visualize_monitor.py <csv_file>
    
Example:
    python3 visualize_monitor.py vast_monitor.csv
"""

import sys
import csv
from datetime import datetime
import matplotlib
matplotlib.use('Agg')  # Use non-interactive backend
import matplotlib.pyplot as plt
from matplotlib.dates import DateFormatter
import matplotlib.dates as mdates

def parse_csv(csv_file):
    """Parse the monitoring CSV file"""
    timestamps = []
    gpu_data = {}
    cpu_util = []
    mem_used = []
    mem_total = []
    load_1min = []
    load_5min = []
    load_15min = []
    
    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)
        
        for row in reader:
            # Parse timestamp
            ts = datetime.strptime(row['timestamp'], '%Y-%m-%d %H:%M:%S')
            timestamps.append(ts)
            
            # Parse GPU data if available
            if row['gpu_id']:
                gpu_id = int(row['gpu_id'])
                if gpu_id not in gpu_data:
                    gpu_data[gpu_id] = {
                        'name': row['gpu_name'],
                        'util': [],
                        'mem_used': [],
                        'mem_total': [],
                        'temp': [],
                        'power': [],
                        'power_limit': []
                    }
                
                gpu_data[gpu_id]['util'].append(float(row['gpu_util_%']))
                gpu_data[gpu_id]['mem_used'].append(float(row['gpu_mem_used_mb']))
                gpu_data[gpu_id]['mem_total'].append(float(row['gpu_mem_total_mb']))
                gpu_data[gpu_id]['temp'].append(float(row['gpu_temp_c']))
                gpu_data[gpu_id]['power'].append(float(row['gpu_power_w']))
                gpu_data[gpu_id]['power_limit'].append(float(row['gpu_power_limit_w']))
            
            # Parse CPU/System data
            cpu_util.append(float(row['cpu_util_%']))
            mem_used.append(float(row['mem_used_mb']))
            mem_total.append(float(row['mem_total_mb']))
            load_1min.append(float(row['load_1min']))
            load_5min.append(float(row['load_5min']))
            load_15min.append(float(row['load_15min']))
    
    return {
        'timestamps': timestamps,
        'gpu_data': gpu_data,
        'cpu': {
            'util': cpu_util,
            'mem_used': mem_used,
            'mem_total': mem_total,
            'load_1min': load_1min,
            'load_5min': load_5min,
            'load_15min': load_15min
        }
    }

def create_visualizations(data, output_prefix='vast_monitor'):
    """Create visualization graphs"""
    timestamps = data['timestamps']
    gpu_data = data['gpu_data']
    cpu = data['cpu']
    
    # Calculate number of subplots needed
    num_gpus = len(gpu_data)
    has_gpu = num_gpus > 0
    
    # Create figure with subplots
    if has_gpu:
        fig, axes = plt.subplots(3, 2, figsize=(16, 12))
        fig.suptitle('Vast.ai Server Resource Monitoring', fontsize=16, fontweight='bold')
    else:
        fig, axes = plt.subplots(2, 2, figsize=(16, 8))
        fig.suptitle('Vast.ai Server Resource Monitoring (CPU Only)', fontsize=16, fontweight='bold')
    
    # Flatten axes for easier indexing
    if has_gpu:
        axes = axes.flatten()
    else:
        axes = [axes[0, 0], axes[0, 1], axes[1, 0], axes[1, 1]]
    
    plot_idx = 0
    
    # Plot GPU Utilization
    if has_gpu:
        ax = axes[plot_idx]
        for gpu_id, gpu in gpu_data.items():
            ax.plot(timestamps, gpu['util'], label=f"GPU {gpu_id}: {gpu['name']}", linewidth=2)
        ax.set_xlabel('Time')
        ax.set_ylabel('GPU Utilization (%)')
        ax.set_title('GPU Utilization', fontweight='bold')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)
        ax.set_ylim(0, 100)
        ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))
        plt.setp(ax.xaxis.get_majorticklabels(), rotation=45, ha='right')
        plot_idx += 1
        
        # Plot GPU Memory
        ax = axes[plot_idx]
        for gpu_id, gpu in gpu_data.items():
            mem_percent = [(used / total * 100) for used, total in zip(gpu['mem_used'], gpu['mem_total'])]
            ax.plot(timestamps, mem_percent, label=f"GPU {gpu_id}", linewidth=2)
        ax.set_xlabel('Time')
        ax.set_ylabel('GPU Memory Usage (%)')
        ax.set_title('GPU Memory Usage', fontweight='bold')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)
        ax.set_ylim(0, 100)
        ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))
        plt.setp(ax.xaxis.get_majorticklabels(), rotation=45, ha='right')
        plot_idx += 1
        
        # Plot GPU Temperature
        ax = axes[plot_idx]
        for gpu_id, gpu in gpu_data.items():
            ax.plot(timestamps, gpu['temp'], label=f"GPU {gpu_id}", linewidth=2)
        ax.set_xlabel('Time')
        ax.set_ylabel('Temperature (°C)')
        ax.set_title('GPU Temperature', fontweight='bold')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)
        ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))
        plt.setp(ax.xaxis.get_majorticklabels(), rotation=45, ha='right')
        plot_idx += 1
        
        # Plot GPU Power
        ax = axes[plot_idx]
        for gpu_id, gpu in gpu_data.items():
            power_limit = gpu['power_limit'][0] if gpu['power_limit'] else 0
            ax.plot(timestamps, gpu['power'], label=f"GPU {gpu_id} (limit: {power_limit:.0f}W)", linewidth=2)
        ax.set_xlabel('Time')
        ax.set_ylabel('Power Draw (W)')
        ax.set_title('GPU Power Consumption', fontweight='bold')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)
        ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))
        plt.setp(ax.xaxis.get_majorticklabels(), rotation=45, ha='right')
        plot_idx += 1
    
    # Plot CPU Utilization
    ax = axes[plot_idx]
    ax.plot(timestamps, cpu['util'], color='#2E86AB', linewidth=2, label='CPU Usage')
    ax.fill_between(timestamps, cpu['util'], alpha=0.3, color='#2E86AB')
    ax.set_xlabel('Time')
    ax.set_ylabel('CPU Utilization (%)')
    ax.set_title('CPU Utilization', fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(0, 100)
    ax.legend(loc='best')
    ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))
    plt.setp(ax.xaxis.get_majorticklabels(), rotation=45, ha='right')
    plot_idx += 1
    
    # Plot System Memory
    ax = axes[plot_idx]
    mem_percent = [(used / total * 100) for used, total in zip(cpu['mem_used'], cpu['mem_total'])]
    ax.plot(timestamps, mem_percent, color='#A23B72', linewidth=2, label='RAM Usage')
    ax.fill_between(timestamps, mem_percent, alpha=0.3, color='#A23B72')
    ax.set_xlabel('Time')
    ax.set_ylabel('System Memory Usage (%)')
    ax.set_title('System Memory Usage', fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(0, 100)
    ax.legend(loc='best')
    ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))
    plt.setp(ax.xaxis.get_majorticklabels(), rotation=45, ha='right')
    plot_idx += 1
    
    # If we still have space (only in GPU mode), plot load average
    if has_gpu and plot_idx < len(axes):
        ax = axes[plot_idx]
        ax.plot(timestamps, cpu['load_1min'], label='1 min', linewidth=2)
        ax.plot(timestamps, cpu['load_5min'], label='5 min', linewidth=2)
        ax.plot(timestamps, cpu['load_15min'], label='15 min', linewidth=2)
        ax.set_xlabel('Time')
        ax.set_ylabel('Load Average')
        ax.set_title('System Load Average', fontweight='bold')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)
        ax.xaxis.set_major_formatter(DateFormatter('%H:%M:%S'))
        plt.setp(ax.xaxis.get_majorticklabels(), rotation=45, ha='right')
    
    plt.tight_layout()
    
    # Save figure
    output_file = f'{output_prefix}.png'
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"\n✅ Visualization saved to: {output_file}")
    
    # Create summary statistics
    print("\n" + "="*70)
    print("MONITORING SUMMARY")
    print("="*70)
    
    if has_gpu:
        print("\nGPU Statistics:")
        for gpu_id, gpu in gpu_data.items():
            print(f"\n  GPU {gpu_id}: {gpu['name']}")
            print(f"    Avg Utilization:  {sum(gpu['util'])/len(gpu['util']):.1f}%")
            print(f"    Max Utilization:  {max(gpu['util']):.1f}%")
            avg_mem_pct = sum(u/t*100 for u, t in zip(gpu['mem_used'], gpu['mem_total'])) / len(gpu['mem_used'])
            print(f"    Avg Memory:       {avg_mem_pct:.1f}%")
            print(f"    Avg Temperature:  {sum(gpu['temp'])/len(gpu['temp']):.1f}°C")
            print(f"    Max Temperature:  {max(gpu['temp']):.1f}°C")
            print(f"    Avg Power:        {sum(gpu['power'])/len(gpu['power']):.1f}W")
    
    print("\n  CPU/System Statistics:")
    print(f"    Avg CPU Usage:    {sum(cpu['util'])/len(cpu['util']):.1f}%")
    print(f"    Max CPU Usage:    {max(cpu['util']):.1f}%")
    avg_mem_pct = sum(u/t*100 for u, t in zip(cpu['mem_used'], cpu['mem_total'])) / len(cpu['mem_used'])
    print(f"    Avg Memory:       {avg_mem_pct:.1f}%")
    print(f"    Avg Load (1min):  {sum(cpu['load_1min'])/len(cpu['load_1min']):.2f}")
    
    print("\n  Monitoring Period:")
    print(f"    Start:            {timestamps[0]}")
    print(f"    End:              {timestamps[-1]}")
    print(f"    Duration:         {(timestamps[-1] - timestamps[0]).total_seconds():.0f} seconds")
    print(f"    Samples:          {len(timestamps)}")
    print("="*70 + "\n")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 visualize_monitor.py <csv_file>")
        print("\nExample:")
        print("  python3 visualize_monitor.py vast_monitor.csv")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    
    try:
        print(f"Loading data from {csv_file}...")
        data = parse_csv(csv_file)
        
        print(f"Creating visualizations...")
        output_prefix = csv_file.replace('.csv', '')
        create_visualizations(data, output_prefix)
        
    except FileNotFoundError:
        print(f"Error: File '{csv_file}' not found")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
