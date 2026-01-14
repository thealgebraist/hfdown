#!/usr/bin/env python3
"""
Resource Monitor for Vast.ai Instances

This script monitors CPU, GPU, memory, and disk usage on Vast.ai instances
and commits the monitoring data to a git repository.
"""

import argparse
import json
import os
import sys
import time
import logging
from datetime import datetime
from pathlib import Path
from typing import Dict, List

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


def get_cpu_info() -> Dict:
    """Get CPU usage information"""
    try:
        import psutil
        
        cpu_percent = psutil.cpu_percent(interval=1, percpu=True)
        cpu_count = psutil.cpu_count()
        
        return {
            "cpu_percent_total": psutil.cpu_percent(interval=1),
            "cpu_percent_per_core": cpu_percent,
            "cpu_count": cpu_count,
            "cpu_freq": psutil.cpu_freq()._asdict() if psutil.cpu_freq() else None
        }
    except Exception as e:
        logger.error(f"Failed to get CPU info: {e}")
        return {}


def get_memory_info() -> Dict:
    """Get memory usage information"""
    try:
        import psutil
        
        mem = psutil.virtual_memory()
        swap = psutil.swap_memory()
        
        return {
            "total_gb": mem.total / (1024**3),
            "available_gb": mem.available / (1024**3),
            "used_gb": mem.used / (1024**3),
            "percent": mem.percent,
            "swap_total_gb": swap.total / (1024**3),
            "swap_used_gb": swap.used / (1024**3),
            "swap_percent": swap.percent
        }
    except Exception as e:
        logger.error(f"Failed to get memory info: {e}")
        return {}


def get_disk_info() -> Dict:
    """Get disk usage information"""
    try:
        import psutil
        
        disk = psutil.disk_usage('/')
        
        return {
            "total_gb": disk.total / (1024**3),
            "used_gb": disk.used / (1024**3),
            "free_gb": disk.free / (1024**3),
            "percent": disk.percent
        }
    except Exception as e:
        logger.error(f"Failed to get disk info: {e}")
        return {}


def get_gpu_info() -> List[Dict]:
    """Get GPU usage information"""
    try:
        import torch
        
        if not torch.cuda.is_available():
            return []
        
        gpu_info = []
        for i in range(torch.cuda.device_count()):
            gpu_info.append({
                "id": i,
                "name": torch.cuda.get_device_name(i),
                "memory_allocated_gb": torch.cuda.memory_allocated(i) / (1024**3),
                "memory_reserved_gb": torch.cuda.memory_reserved(i) / (1024**3),
                "memory_total_gb": torch.cuda.get_device_properties(i).total_memory / (1024**3)
            })
        
        return gpu_info
        
    except ImportError:
        # Try nvidia-smi as fallback
        try:
            import subprocess
            result = subprocess.run(
                ["nvidia-smi", "--query-gpu=index,name,memory.used,memory.total,utilization.gpu", "--format=csv,noheader,nounits"],
                capture_output=True,
                text=True
            )
            
            if result.returncode == 0:
                gpu_info = []
                for line in result.stdout.strip().split('\n'):
                    if line:
                        parts = [p.strip() for p in line.split(',')]
                        if len(parts) >= 5:
                            gpu_info.append({
                                "id": int(parts[0]),
                                "name": parts[1],
                                "memory_used_mb": float(parts[2]),
                                "memory_total_mb": float(parts[3]),
                                "utilization_percent": float(parts[4])
                            })
                return gpu_info
        except Exception as e:
            logger.warning(f"Failed to get GPU info via nvidia-smi: {e}")
    except Exception as e:
        logger.error(f"Failed to get GPU info: {e}")
    
    return []


def get_process_info() -> List[Dict]:
    """Get information about running processes"""
    try:
        import psutil
        
        processes = []
        for proc in psutil.process_iter(['pid', 'name', 'cpu_percent', 'memory_percent']):
            try:
                # Filter for Python processes that might be our generators
                if 'python' in proc.info['name'].lower():
                    processes.append({
                        "pid": proc.info['pid'],
                        "name": proc.info['name'],
                        "cpu_percent": proc.info['cpu_percent'],
                        "memory_percent": proc.info['memory_percent']
                    })
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        
        return processes
    except Exception as e:
        logger.error(f"Failed to get process info: {e}")
        return []


def collect_metrics() -> Dict:
    """Collect all system metrics"""
    timestamp = datetime.now().isoformat()
    
    metrics = {
        "timestamp": timestamp,
        "cpu": get_cpu_info(),
        "memory": get_memory_info(),
        "disk": get_disk_info(),
        "gpu": get_gpu_info(),
        "processes": get_process_info()
    }
    
    return metrics


def save_metrics(metrics: Dict, output_dir: str):
    """Save metrics to JSON file"""
    try:
        os.makedirs(output_dir, exist_ok=True)
        
        # Create filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"metrics_{timestamp}.json"
        filepath = os.path.join(output_dir, filename)
        
        # Save metrics
        with open(filepath, 'w') as f:
            json.dump(metrics, f, indent=2)
        
        logger.info(f"Saved metrics to {filepath}")
        return filepath
        
    except Exception as e:
        logger.error(f"Failed to save metrics: {e}")
        return None


def append_to_timeseries(metrics: Dict, output_dir: str):
    """Append metrics to a timeseries CSV file"""
    try:
        import csv
        
        csv_file = os.path.join(output_dir, "resource_timeseries.csv")
        
        # Check if file exists to determine if we need header
        file_exists = os.path.exists(csv_file)
        
        with open(csv_file, 'a', newline='') as f:
            writer = csv.writer(f)
            
            # Write header if new file
            if not file_exists:
                writer.writerow([
                    "timestamp",
                    "cpu_percent",
                    "memory_percent",
                    "disk_percent",
                    "gpu_count",
                    "gpu_memory_used_gb",
                    "gpu_utilization_percent"
                ])
            
            # Calculate GPU totals
            gpu_count = len(metrics.get("gpu", []))
            gpu_memory_used = sum(gpu.get("memory_allocated_gb", gpu.get("memory_used_mb", 0) / 1024) 
                                 for gpu in metrics.get("gpu", []))
            gpu_util = sum(gpu.get("utilization_percent", 0) for gpu in metrics.get("gpu", [])) / max(gpu_count, 1)
            
            # Write data row
            writer.writerow([
                metrics["timestamp"],
                metrics.get("cpu", {}).get("cpu_percent_total", 0),
                metrics.get("memory", {}).get("percent", 0),
                metrics.get("disk", {}).get("percent", 0),
                gpu_count,
                round(gpu_memory_used, 2),
                round(gpu_util, 2)
            ])
        
        logger.debug(f"Appended metrics to {csv_file}")
        
    except Exception as e:
        logger.error(f"Failed to append to timeseries: {e}")


def git_commit_and_push(output_dir: str, message: str, push: bool = False):
    """Commit metrics to git and optionally push"""
    try:
        import git
        
        repo_path = Path(output_dir)
        
        # Check if git repo exists
        if not (repo_path / ".git").exists():
            logger.warning(f"No git repository found in {output_dir}")
            return
        
        repo = git.Repo(repo_path)
        
        # Add all new files
        repo.git.add(A=True)
        
        # Check if there are changes to commit
        if repo.is_dirty() or repo.untracked_files:
            repo.index.commit(message)
            logger.info(f"Committed metrics: {message}")
            
            if push:
                origin = repo.remote(name='origin')
                origin.push()
                logger.info("Pushed metrics to remote")
        else:
            logger.debug("No new metrics to commit")
            
    except Exception as e:
        logger.error(f"Git commit/push failed: {e}")


def monitor_resources(
    output_dir: str,
    interval: int = 60,
    git_auto_commit: bool = False,
    git_push: bool = False,
    duration: int = None
):
    """
    Monitor resources and save metrics periodically
    
    Args:
        output_dir: Directory to save metrics
        interval: Time between measurements in seconds
        git_auto_commit: Whether to auto-commit metrics
        git_push: Whether to push commits
        duration: Total monitoring duration in seconds (None for infinite)
    """
    logger.info(f"Starting resource monitoring (interval: {interval}s)")
    
    os.makedirs(output_dir, exist_ok=True)
    
    start_time = time.time()
    iteration = 0
    
    try:
        while True:
            # Check duration limit
            if duration and (time.time() - start_time) >= duration:
                logger.info("Monitoring duration reached")
                break
            
            # Collect metrics
            metrics = collect_metrics()
            
            # Save to file
            save_metrics(metrics, output_dir)
            
            # Append to timeseries
            append_to_timeseries(metrics, output_dir)
            
            iteration += 1
            
            # Commit periodically (every 10 iterations)
            if git_auto_commit and iteration % 10 == 0:
                git_commit_and_push(
                    output_dir,
                    f"Resource metrics update (iteration {iteration})",
                    git_push
                )
            
            # Log summary
            cpu_pct = metrics.get("cpu", {}).get("cpu_percent_total", 0)
            mem_pct = metrics.get("memory", {}).get("percent", 0)
            gpu_count = len(metrics.get("gpu", []))
            
            logger.info(
                f"Metrics collected - CPU: {cpu_pct:.1f}%, "
                f"Memory: {mem_pct:.1f}%, "
                f"GPUs: {gpu_count}"
            )
            
            # Sleep
            time.sleep(interval)
            
    except KeyboardInterrupt:
        logger.info("Monitoring stopped by user")
    except Exception as e:
        logger.error(f"Monitoring error: {e}", exc_info=True)
    finally:
        # Final commit
        if git_auto_commit:
            git_commit_and_push(
                output_dir,
                f"Final resource metrics (iteration {iteration})",
                git_push
            )
        
        logger.info(f"Monitoring complete ({iteration} iterations)")


def main():
    parser = argparse.ArgumentParser(
        description="Monitor system resources and commit to git"
    )
    parser.add_argument(
        "--output-dir",
        default="resource_metrics",
        help="Output directory for metrics"
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=60,
        help="Time between measurements in seconds"
    )
    parser.add_argument(
        "--duration",
        type=int,
        help="Total monitoring duration in seconds (default: infinite)"
    )
    parser.add_argument(
        "--git-auto-commit",
        action="store_true",
        help="Automatically commit metrics to git"
    )
    parser.add_argument(
        "--git-push",
        action="store_true",
        help="Push commits to remote (requires --git-auto-commit)"
    )
    
    args = parser.parse_args()
    
    # Run monitoring
    monitor_resources(
        args.output_dir,
        args.interval,
        args.git_auto_commit,
        args.git_push,
        args.duration
    )


if __name__ == "__main__":
    main()
