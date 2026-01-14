#!/usr/bin/env python3
"""
Flux1 Schnell Image Generator with Multi-Process Support

This script generates images using Flux1 Schnell model with vanilla PyTorch,
supports multiple processes for parallel generation, monitors process health,
and automatically commits results to git.
"""

import argparse
import multiprocessing as mp
import os
import sys
import time
import logging
from pathlib import Path
from typing import List, Optional
import traceback
import json
from datetime import datetime

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(processName)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


def setup_flux_model():
    """
    Setup and load Flux1 Schnell model using vanilla PyTorch
    
    Returns:
        Loaded pipeline object
    """
    try:
        import torch
        from diffusers import FluxPipeline
        
        logger.info("Loading Flux1 Schnell model...")
        
        # Check CUDA availability
        if torch.cuda.is_available():
            device = "cuda"
            logger.info(f"CUDA available: {torch.cuda.get_device_name(0)}")
            logger.info(f"GPU Memory: {torch.cuda.get_device_properties(0).total_memory / 1e9:.2f} GB")
        else:
            device = "cpu"
            logger.warning("CUDA not available, using CPU (will be very slow)")
        
        # Load the model
        pipe = FluxPipeline.from_pretrained(
            "black-forest-labs/FLUX.1-schnell",
            torch_dtype=torch.bfloat16 if device == "cuda" else torch.float32
        )
        
        # Move to GPU if available
        pipe = pipe.to(device)
        
        # Enable memory optimizations
        if device == "cuda":
            try:
                pipe.enable_model_cpu_offload()
                logger.info("Enabled CPU offload for memory efficiency")
            except Exception as e:
                logger.warning(f"Could not enable CPU offload: {e}")
        
        logger.info(f"Model loaded successfully on {device}")
        return pipe
        
    except Exception as e:
        logger.error(f"Failed to load model: {e}")
        raise


def generate_image(pipe, prompt: str, output_path: str, num_inference_steps: int = 4):
    """
    Generate a single image from a prompt
    
    Args:
        pipe: Flux pipeline object
        prompt: Text prompt for generation
        output_path: Where to save the generated image
        num_inference_steps: Number of inference steps (default 4 for schnell)
    """
    try:
        logger.info(f"Generating image for: {prompt[:50]}...")
        
        # Generate image
        image = pipe(
            prompt,
            num_inference_steps=num_inference_steps,
            guidance_scale=0.0  # Schnell doesn't need guidance
        ).images[0]
        
        # Save image
        image.save(output_path)
        logger.info(f"Image saved to: {output_path}")
        
        return True
        
    except Exception as e:
        logger.error(f"Failed to generate image: {e}")
        return False


def worker_process(
    worker_id: int,
    prompts: List[str],
    output_dir: str,
    status_queue: mp.Queue,
    num_inference_steps: int = 4
):
    """
    Worker process for generating images
    
    Args:
        worker_id: ID of this worker
        prompts: List of prompts to process
        output_dir: Output directory for images
        status_queue: Queue for reporting status
        num_inference_steps: Number of inference steps
    """
    try:
        logger.info(f"Worker {worker_id} starting with {len(prompts)} prompts")
        
        # Load model (each process loads its own copy)
        pipe = setup_flux_model()
        
        # Process prompts
        for idx, prompt in enumerate(prompts):
            try:
                # Create output filename
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                filename = f"worker{worker_id}_image{idx}_{timestamp}.png"
                output_path = os.path.join(output_dir, filename)
                
                # Generate image
                success = generate_image(pipe, prompt, output_path, num_inference_steps)
                
                # Report status
                status_queue.put({
                    "worker_id": worker_id,
                    "prompt_idx": idx,
                    "prompt": prompt,
                    "output_path": output_path,
                    "success": success,
                    "timestamp": timestamp
                })
                
            except Exception as e:
                logger.error(f"Worker {worker_id} failed on prompt {idx}: {e}")
                status_queue.put({
                    "worker_id": worker_id,
                    "prompt_idx": idx,
                    "prompt": prompt,
                    "success": False,
                    "error": str(e),
                    "timestamp": datetime.now().strftime("%Y%m%d_%H%M%S")
                })
        
        logger.info(f"Worker {worker_id} completed all prompts")
        status_queue.put({
            "worker_id": worker_id,
            "status": "completed"
        })
        
    except Exception as e:
        logger.error(f"Worker {worker_id} crashed: {e}")
        logger.error(traceback.format_exc())
        status_queue.put({
            "worker_id": worker_id,
            "status": "crashed",
            "error": str(e)
        })


def git_commit_and_push(output_dir: str, message: str, push: bool = False):
    """
    Commit generated images to git and optionally push
    
    Args:
        output_dir: Directory containing generated images
        message: Commit message
        push: Whether to push to remote
    """
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
            logger.info(f"Committed changes: {message}")
            
            if push:
                origin = repo.remote(name='origin')
                origin.push()
                logger.info("Pushed changes to remote")
        else:
            logger.info("No changes to commit")
            
    except Exception as e:
        logger.error(f"Git commit/push failed: {e}")


def run_generation(
    prompts_file: str,
    output_dir: str,
    num_processes: int = 4,
    num_inference_steps: int = 4,
    git_auto_commit: bool = False,
    git_push: bool = False
):
    """
    Run the complete generation process with multi-process support
    
    Args:
        prompts_file: Path to file containing prompts
        output_dir: Directory for output images
        num_processes: Number of parallel processes
        num_inference_steps: Number of inference steps
        git_auto_commit: Whether to auto-commit results
        git_push: Whether to push commits
    """
    # Load prompts
    logger.info(f"Loading prompts from {prompts_file}")
    with open(prompts_file, 'r') as f:
        prompts = [line.strip() for line in f if line.strip()]
    
    logger.info(f"Loaded {len(prompts)} prompts")
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Distribute prompts across workers
    prompts_per_worker = len(prompts) // num_processes
    remainder = len(prompts) % num_processes
    
    worker_prompts = []
    start_idx = 0
    
    for i in range(num_processes):
        # Give extra prompts to first workers if there's a remainder
        end_idx = start_idx + prompts_per_worker + (1 if i < remainder else 0)
        worker_prompts.append(prompts[start_idx:end_idx])
        start_idx = end_idx
    
    # Create status queue for monitoring
    status_queue = mp.Queue()
    
    # Start worker processes
    processes = []
    for worker_id in range(num_processes):
        p = mp.Process(
            target=worker_process,
            args=(worker_id, worker_prompts[worker_id], output_dir, status_queue, num_inference_steps)
        )
        p.start()
        processes.append(p)
        logger.info(f"Started worker {worker_id} with {len(worker_prompts[worker_id])} prompts")
    
    # Monitor status
    completed_workers = 0
    crashed_workers = 0
    total_images = 0
    failed_images = 0
    
    status_log = []
    
    while completed_workers + crashed_workers < num_processes:
        try:
            # Get status with timeout
            status = status_queue.get(timeout=10)
            status_log.append(status)
            
            if status.get("status") == "completed":
                completed_workers += 1
                logger.info(f"Worker {status['worker_id']} completed ({completed_workers}/{num_processes})")
            elif status.get("status") == "crashed":
                crashed_workers += 1
                logger.error(f"Worker {status['worker_id']} crashed: {status.get('error', 'Unknown error')}")
            elif status.get("success") is True:
                total_images += 1
                logger.info(f"Generated image {total_images}: {status['output_path']}")
                
                # Commit after each image if auto-commit enabled
                if git_auto_commit and total_images % 5 == 0:  # Commit every 5 images
                    git_commit_and_push(
                        output_dir,
                        f"Generated {total_images} images",
                        git_push
                    )
            elif status.get("success") is False:
                failed_images += 1
                logger.warning(f"Failed to generate image for prompt: {status.get('prompt', 'unknown')}")
                
        except Exception:
            # Timeout - check if processes are still alive
            for i, p in enumerate(processes):
                if not p.is_alive() and p.exitcode != 0:
                    logger.error(f"Worker {i} died unexpectedly")
    
    # Wait for all processes to finish
    for p in processes:
        p.join(timeout=10)
        if p.is_alive():
            p.terminate()
    
    # Final commit
    if git_auto_commit:
        git_commit_and_push(
            output_dir,
            f"Generation complete: {total_images} images generated, {failed_images} failed",
            git_push
        )
    
    # Save status log
    status_file = os.path.join(output_dir, f"generation_status_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json")
    with open(status_file, 'w') as f:
        json.dump(status_log, f, indent=2)
    
    # Summary
    logger.info("=" * 60)
    logger.info("Generation Summary:")
    logger.info(f"  Total prompts: {len(prompts)}")
    logger.info(f"  Successful: {total_images}")
    logger.info(f"  Failed: {failed_images}")
    logger.info(f"  Workers completed: {completed_workers}")
    logger.info(f"  Workers crashed: {crashed_workers}")
    logger.info("=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description="Generate images using Flux1 Schnell with multi-process support"
    )
    parser.add_argument(
        "--prompts",
        required=True,
        help="Path to file containing prompts (one per line)"
    )
    parser.add_argument(
        "--output-dir",
        default="generated_images",
        help="Output directory for generated images"
    )
    parser.add_argument(
        "--num-processes",
        type=int,
        default=4,
        help="Number of parallel processes"
    )
    parser.add_argument(
        "--num-inference-steps",
        type=int,
        default=4,
        help="Number of inference steps (default 4 for schnell)"
    )
    parser.add_argument(
        "--git-auto-commit",
        action="store_true",
        help="Automatically commit generated images to git"
    )
    parser.add_argument(
        "--git-push",
        action="store_true",
        help="Push commits to remote (requires --git-auto-commit)"
    )
    
    args = parser.parse_args()
    
    # Validate inputs
    if not os.path.exists(args.prompts):
        logger.error(f"Prompts file not found: {args.prompts}")
        sys.exit(1)
    
    # Run generation
    run_generation(
        args.prompts,
        args.output_dir,
        args.num_processes,
        args.num_inference_steps,
        args.git_auto_commit,
        args.git_push
    )


if __name__ == "__main__":
    main()
