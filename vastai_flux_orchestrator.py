#!/usr/bin/env python3
"""
Vast.ai Flux1 Schnell Orchestration Tool

This script orchestrates the complete workflow for running Flux1 Schnell image generation
on a Vast.ai GPU instance, including:
- Remote package installation
- Multi-process prompt generation with monitoring
- Automatic git commit/push of results
- Resource monitoring
- Automatic server shutdown
"""

import argparse
import subprocess
import sys
import time
import json
import os
from pathlib import Path
from typing import List, Dict, Optional
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class VastAIOrchestrator:
    """Orchestrates Flux1 Schnell generation on Vast.ai instances"""
    
    def __init__(self, ssh_command: str, config_path: Optional[str] = None):
        """
        Initialize the orchestrator
        
        Args:
            ssh_command: SSH command to connect to Vast.ai instance (e.g., 'ssh -p 12345 root@1.2.3.4')
            config_path: Path to configuration file
        """
        self.ssh_command = ssh_command
        self.config = self._load_config(config_path)
        self.instance_id = None
        
    def _load_config(self, config_path: Optional[str]) -> Dict:
        """Load configuration from file or use defaults"""
        default_config = {
            "remote_workspace": "/workspace",
            "git_repo_url": None,
            "git_branch": "main",
            "num_processes": 4,
            "prompts_file": "prompts.txt",
            "output_dir": "generated_images",
            "resource_monitor_interval": 60,
            "auto_shutdown": True,
            "requirements": [
                "torch",
                "torchvision",
                "diffusers",
                "transformers",
                "accelerate",
                "safetensors",
                "pillow",
                "psutil",
                "gitpython"
            ]
        }
        
        if config_path and os.path.exists(config_path):
            with open(config_path, 'r') as f:
                user_config = json.load(f)
                default_config.update(user_config)
        
        return default_config
    
    def test_connection(self) -> bool:
        """
        Test SSH connection to the Vast.ai instance
        
        Returns:
            True if connection successful, False otherwise
        """
        try:
            logger.info("Testing SSH connection...")
            result = self.run_remote_command("echo 'Connection successful'", check=False)
            
            if result.returncode == 0:
                logger.info("SSH connection verified")
                return True
            else:
                logger.error("SSH connection failed")
                logger.error(f"stderr: {result.stderr}")
                return False
                
        except Exception as e:
            logger.error(f"Connection test failed: {e}")
            return False
    
    def run_remote_command(self, command: str, check: bool = True) -> subprocess.CompletedProcess:
        """
        Execute a command on the remote Vast.ai instance
        
        Args:
            command: Command to execute
            check: Whether to check return code
            
        Returns:
            CompletedProcess object
        """
        full_command = f"{self.ssh_command} '{command}'"
        logger.info(f"Executing remote: {command}")
        
        result = subprocess.run(
            full_command,
            shell=True,
            capture_output=True,
            text=True,
            check=False
        )
        
        if check and result.returncode != 0:
            logger.error(f"Command failed: {command}")
            logger.error(f"stdout: {result.stdout}")
            logger.error(f"stderr: {result.stderr}")
            raise RuntimeError(f"Remote command failed: {command}")
        
        return result
    
    def copy_to_remote(self, local_path: str, remote_path: str):
        """Copy file to remote instance"""
        import shlex
        
        # Extract SSH details from ssh_command using proper shell parsing
        ssh_parts = shlex.split(self.ssh_command)
        port = None
        host = None
        
        # Parse SSH command options
        i = 0
        while i < len(ssh_parts):
            part = ssh_parts[i]
            
            # Handle -p or -P with space
            if part in ['-p', '-P'] and i + 1 < len(ssh_parts):
                port = ssh_parts[i + 1]
                i += 2
            # Handle -p12345 or -P12345 (no space)
            elif part.startswith('-p') and len(part) > 2:
                port = part[2:]
                i += 1
            elif part.startswith('-P') and len(part) > 2:
                port = part[2:]
                i += 1
            # Find host (contains @)
            elif '@' in part:
                host = part
                i += 1
            else:
                i += 1
        
        # Build scp command
        if port and host:
            scp_command = f"scp -P {port} {shlex.quote(local_path)} {host}:{shlex.quote(remote_path)}"
        elif host:
            scp_command = f"scp {shlex.quote(local_path)} {host}:{shlex.quote(remote_path)}"
        else:
            raise ValueError("Could not extract host from SSH command")
        
        logger.info(f"Copying {local_path} to {remote_path}")
        subprocess.run(scp_command, shell=True, check=True)
    
    def check_disk_space(self, min_gb: float = 50.0) -> bool:
        """
        Check if remote instance has sufficient disk space
        
        Args:
            min_gb: Minimum required GB of free space
            
        Returns:
            True if sufficient space available, False otherwise
        """
        try:
            logger.info("Checking disk space...")
            result = self.run_remote_command("df -BG / | tail -n 1", check=False)
            
            if result.returncode == 0:
                logger.info(f"Disk usage: {result.stdout.strip()}")
                
                # Parse available space from df output (in GB)
                parts = result.stdout.split()
                if len(parts) >= 4:
                    avail_str = parts[3].rstrip('G')  # Remove 'G' suffix
                    try:
                        avail_gb = float(avail_str)
                        logger.info(f"Available disk space: {avail_gb:.1f} GB")
                        
                        if avail_gb >= min_gb:
                            logger.info(f"Sufficient disk space available ({avail_gb:.1f} GB >= {min_gb} GB)")
                            return True
                        else:
                            logger.warning(f"Low disk space: {avail_gb:.1f} GB < {min_gb} GB required")
                            return False
                    except ValueError:
                        logger.warning(f"Could not parse disk space value: {avail_str}")
            
            return True  # Assume OK if we can't check
            
        except Exception as e:
            logger.warning(f"Could not check disk space: {e}")
            return True  # Assume OK if we can't check
    
    def install_packages(self):
        """Install required packages on the remote instance"""
        logger.info("Installing required packages...")
        
        # Update pip
        self.run_remote_command("pip install --upgrade pip")
        
        # Install packages
        requirements = " ".join(self.config["requirements"])
        self.run_remote_command(f"pip install {requirements}")
        
        logger.info("Package installation complete")
    
    def setup_workspace(self):
        """Setup remote workspace directory structure"""
        logger.info("Setting up workspace...")
        
        workspace = self.config["remote_workspace"]
        output_dir = f"{workspace}/{self.config['output_dir']}"
        
        self.run_remote_command(f"mkdir -p {workspace}")
        self.run_remote_command(f"mkdir -p {output_dir}")
        
        logger.info("Workspace setup complete")
    
    def setup_git_repo(self):
        """Setup git repository for output commits"""
        if not self.config.get("git_repo_url"):
            logger.warning("No git repository URL configured, skipping git setup")
            return
        
        logger.info("Setting up git repository...")
        
        workspace = self.config["remote_workspace"]
        output_dir = f"{workspace}/{self.config['output_dir']}"
        
        # Initialize git repo if not exists
        self.run_remote_command(f"cd {output_dir} && (git init || true)")
        
        # Set git config if provided
        if self.config.get("git_user_name"):
            self.run_remote_command(
                f"cd {output_dir} && git config user.name '{self.config['git_user_name']}'"
            )
        if self.config.get("git_user_email"):
            self.run_remote_command(
                f"cd {output_dir} && git config user.email '{self.config['git_user_email']}'"
            )
        
        # Add remote if provided
        repo_url = self.config.get("git_repo_url")
        if repo_url:
            self.run_remote_command(
                f"cd {output_dir} && (git remote add origin {repo_url} || git remote set-url origin {repo_url})"
            )
        
        logger.info("Git repository setup complete")
    
    def upload_scripts(self):
        """Upload generator and monitor scripts to remote instance"""
        logger.info("Uploading scripts to remote instance...")
        
        workspace = self.config["remote_workspace"]
        
        # Create generator script path
        generator_script = Path(__file__).parent / "flux_generator.py"
        monitor_script = Path(__file__).parent / "resource_monitor.py"
        
        if generator_script.exists():
            self.copy_to_remote(str(generator_script), f"{workspace}/flux_generator.py")
        
        if monitor_script.exists():
            self.copy_to_remote(str(monitor_script), f"{workspace}/resource_monitor.py")
        
        logger.info("Scripts uploaded")
    
    def upload_prompts(self, prompts_file: str):
        """Upload prompts file to remote instance"""
        logger.info(f"Uploading prompts from {prompts_file}...")
        
        workspace = self.config["remote_workspace"]
        self.copy_to_remote(prompts_file, f"{workspace}/prompts.txt")
        
        logger.info("Prompts uploaded")
    
    def start_generation(self, prompts_file: str):
        """Start the generation process on remote instance"""
        logger.info("Starting generation process...")
        
        workspace = self.config["remote_workspace"]
        num_processes = self.config["num_processes"]
        output_dir = self.config["output_dir"]
        
        # Build the command to run generator
        generator_cmd = (
            f"cd {workspace} && "
            f"python3 flux_generator.py "
            f"--prompts prompts.txt "
            f"--output-dir {output_dir} "
            f"--num-processes {num_processes} "
            f"--git-auto-commit"
        )
        
        if self.config.get("git_repo_url"):
            generator_cmd += " --git-push"
        
        # Run in background with nohup
        self.run_remote_command(
            f"nohup {generator_cmd} > {workspace}/generator.log 2>&1 &"
        )
        
        logger.info("Generation process started")
    
    def start_resource_monitor(self):
        """Start the resource monitoring process"""
        logger.info("Starting resource monitor...")
        
        workspace = self.config["remote_workspace"]
        interval = self.config["resource_monitor_interval"]
        output_dir = self.config["output_dir"]
        
        monitor_cmd = (
            f"cd {workspace} && "
            f"python3 resource_monitor.py "
            f"--output-dir {output_dir} "
            f"--interval {interval} "
            f"--git-auto-commit"
        )
        
        if self.config.get("git_repo_url"):
            monitor_cmd += " --git-push"
        
        # Run in background with nohup
        self.run_remote_command(
            f"nohup {monitor_cmd} > {workspace}/monitor.log 2>&1 &"
        )
        
        logger.info("Resource monitor started")
    
    def monitor_progress(self) -> bool:
        """
        Monitor the generation progress
        
        Returns:
            True if generation completed successfully, False otherwise
        """
        logger.info("Monitoring generation progress...")
        
        workspace = self.config["remote_workspace"]
        
        while True:
            # Check if generator is still running
            result = self.run_remote_command(
                f"pgrep -f flux_generator.py",
                check=False
            )
            
            if result.returncode != 0:
                logger.info("Generator process has completed")
                break
            
            # Print status
            log_result = self.run_remote_command(
                f"tail -n 5 {workspace}/generator.log",
                check=False
            )
            if log_result.stdout:
                logger.info(f"Generator status:\n{log_result.stdout}")
            
            time.sleep(30)
        
        # Check for errors in the log
        log_result = self.run_remote_command(
            f"tail -n 50 {workspace}/generator.log",
            check=False
        )
        
        if "error" in log_result.stdout.lower() or "failed" in log_result.stdout.lower():
            logger.warning("Detected errors in generator log")
            return False
        
        return True
    
    def shutdown_instance(self):
        """Shutdown the Vast.ai instance"""
        if not self.config.get("auto_shutdown", True):
            logger.info("Auto-shutdown disabled, skipping")
            return
        
        logger.info("Shutting down Vast.ai instance...")
        
        # Stop resource monitor
        self.run_remote_command("pkill -f resource_monitor.py", check=False)
        
        # Shutdown command
        self.run_remote_command("shutdown -h now", check=False)
        
        logger.info("Shutdown command sent")
    
    def run(self, prompts_file: str):
        """
        Run the complete orchestration workflow
        
        Args:
            prompts_file: Path to file containing prompts (one per line)
        """
        try:
            logger.info("Starting Vast.ai Flux1 Schnell orchestration...")
            
            # Test connection first
            if not self.test_connection():
                raise RuntimeError("Failed to connect to Vast.ai instance")
            
            # Check disk space
            if not self.check_disk_space():
                logger.warning("Low disk space detected, but continuing...")
            
            # Setup
            self.setup_workspace()
            self.install_packages()
            self.setup_git_repo()
            
            # Upload files
            self.upload_scripts()
            self.upload_prompts(prompts_file)
            
            # Start processes
            self.start_resource_monitor()
            self.start_generation(prompts_file)
            
            # Monitor
            success = self.monitor_progress()
            
            # Cleanup
            if success:
                logger.info("Generation completed successfully")
            else:
                logger.warning("Generation completed with errors")
            
            # Shutdown
            if self.config.get("auto_shutdown", True):
                self.shutdown_instance()
            
            logger.info("Orchestration complete")
            
        except Exception as e:
            logger.error(f"Orchestration failed: {e}", exc_info=True)
            raise


def main():
    parser = argparse.ArgumentParser(
        description="Orchestrate Flux1 Schnell generation on Vast.ai instances"
    )
    parser.add_argument(
        "ssh_command",
        help="SSH command to connect to Vast.ai instance (e.g., 'ssh -p 12345 root@1.2.3.4')"
    )
    parser.add_argument(
        "prompts_file",
        help="Path to file containing prompts (one per line)"
    )
    parser.add_argument(
        "--config",
        help="Path to JSON configuration file"
    )
    
    args = parser.parse_args()
    
    # Validate prompts file exists
    if not os.path.exists(args.prompts_file):
        logger.error(f"Prompts file not found: {args.prompts_file}")
        sys.exit(1)
    
    # Create orchestrator and run
    orchestrator = VastAIOrchestrator(args.ssh_command, args.config)
    orchestrator.run(args.prompts_file)


if __name__ == "__main__":
    main()
