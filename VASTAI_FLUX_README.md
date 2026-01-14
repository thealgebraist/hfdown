# Vast.ai Flux1 Schnell Orchestration

This tool provides complete orchestration for running Flux1 Schnell image generation on Vast.ai GPU instances. It handles remote setup, multi-process generation, monitoring, automatic git commits, and server shutdown.

## Features

- 🚀 **Automated Setup**: Remotely installs all required packages on Vast.ai instance
- 🎨 **Flux1 Schnell**: Uses vanilla PyTorch to run Flux1 Schnell model for fast image generation
- ⚡ **Multi-Process**: Parallel generation using multiple processes for maximum GPU utilization
- 📊 **Process Monitoring**: Tracks whether processes crash or succeed
- 🔄 **Auto Git Commits**: Automatically commits and pushes generated images to GitHub
- 📈 **Resource Monitoring**: Tracks CPU, GPU, memory usage and commits metrics to GitHub
- 🛑 **Auto Shutdown**: Automatically shuts down Vast.ai instance when complete to save costs

## Quick Start

### 1. Prerequisites

- Python 3.8+
- A Vast.ai account with a GPU instance
- Git repository for storing outputs (optional but recommended)

### 2. Local Setup

Install required packages locally:
```bash
pip install psutil gitpython
```

### 3. Create Configuration File

Copy the example configuration:
```bash
cp vastai_config.example.json vastai_config.json
```

Edit `vastai_config.json` with your settings:
```json
{
  "git_repo_url": "https://github.com/yourusername/flux-outputs.git",
  "git_user_name": "Your Name",
  "git_user_email": "your.email@example.com",
  "num_processes": 4
}
```

### 4. Create Prompts File

Create a text file with prompts (one per line):
```bash
cat > my_prompts.txt << EOF
A serene mountain landscape at sunset
A futuristic city with flying cars
A cozy coffee shop in autumn
EOF
```

Or use the example:
```bash
cp prompts.example.txt my_prompts.txt
```

### 5. Start a Vast.ai Instance

1. Go to [Vast.ai](https://vast.ai/)
2. Find and rent a GPU instance (recommend RTX 3090 or better)
3. Copy the SSH command (e.g., `ssh -p 12345 root@1.2.3.4`)

### 6. Run the Orchestrator

```bash
python3 vastai_flux_orchestrator.py \
  "ssh -p 12345 root@1.2.3.4" \
  my_prompts.txt \
  --config vastai_config.json
```

The orchestrator will:
1. Connect to your Vast.ai instance
2. Install PyTorch, Diffusers, and dependencies
3. Upload the generator and monitor scripts
4. Start the resource monitor
5. Start image generation with multiple processes
6. Monitor progress and commit results
7. Shutdown the instance when complete

## Components

### 1. Main Orchestrator (`vastai_flux_orchestrator.py`)

The main script that coordinates everything:
- Connects to Vast.ai instance via SSH
- Installs packages remotely
- Uploads scripts and prompts
- Starts generator and monitor
- Monitors progress
- Shuts down instance

**Usage:**
```bash
python3 vastai_flux_orchestrator.py <ssh_command> <prompts_file> [--config CONFIG]
```

### 2. Flux Generator (`flux_generator.py`)

Runs on the remote instance to generate images:
- Loads Flux1 Schnell model
- Distributes prompts across multiple processes
- Monitors process health
- Commits generated images to git

**Remote Usage:**
```bash
python3 flux_generator.py \
  --prompts prompts.txt \
  --output-dir generated_images \
  --num-processes 4 \
  --git-auto-commit \
  --git-push
```

### 3. Resource Monitor (`resource_monitor.py`)

Runs on the remote instance to track resource usage:
- Monitors CPU, GPU, memory, disk
- Tracks running processes
- Saves metrics as JSON and CSV
- Commits metrics to git

**Remote Usage:**
```bash
python3 resource_monitor.py \
  --output-dir resource_metrics \
  --interval 60 \
  --git-auto-commit \
  --git-push
```

## Configuration Options

### Orchestrator Configuration (`vastai_config.json`)

| Option | Description | Default |
|--------|-------------|---------|
| `remote_workspace` | Remote directory for all files | `/workspace` |
| `git_repo_url` | GitHub repository URL for outputs | `null` |
| `git_branch` | Git branch to use | `main` |
| `git_user_name` | Git commit author name | Required if using git |
| `git_user_email` | Git commit author email | Required if using git |
| `num_processes` | Number of parallel processes | `4` |
| `output_dir` | Directory for generated images | `generated_images` |
| `resource_monitor_interval` | Seconds between metrics | `60` |
| `auto_shutdown` | Auto shutdown when complete | `true` |
| `requirements` | Python packages to install | See example |

## Advanced Usage

### Without Git Integration

If you don't want to use git:
```json
{
  "git_repo_url": null,
  "auto_shutdown": false
}
```

Then manually download results:
```bash
scp -P 12345 root@1.2.3.4:/workspace/generated_images/* ./local_outputs/
```

### Custom Number of Inference Steps

Edit the generator call in orchestrator or run manually:
```bash
python3 flux_generator.py --prompts prompts.txt --num-inference-steps 8
```

### Monitor Without Shutdown

To keep instance running for inspection:
```json
{
  "auto_shutdown": false
}
```

### Multiple GPU Support

The system automatically detects and uses all available GPUs. Each process will use CUDA device(s) as available.

## Workflow Diagram

```
Local Machine                      Vast.ai Instance
─────────────                      ────────────────
    │
    │ 1. Connect via SSH
    ├──────────────────────────────►
    │                                  │
    │ 2. Install packages               │
    ├──────────────────────────────► pip install torch diffusers ...
    │                                  │
    │ 3. Upload scripts                 │
    ├──────────────────────────────► flux_generator.py
    ├──────────────────────────────► resource_monitor.py
    │                                  │
    │ 4. Upload prompts                 │
    ├──────────────────────────────► prompts.txt
    │                                  │
    │ 5. Start monitor                  │
    ├──────────────────────────────► python resource_monitor.py &
    │                                  │
    │ 6. Start generator                │
    ├──────────────────────────────► python flux_generator.py &
    │                                  │
    │                                  ├─► Process 1: Generate images
    │                                  ├─► Process 2: Generate images
    │                                  ├─► Process 3: Generate images
    │                                  └─► Process 4: Generate images
    │                                  │
    │ 7. Monitor progress               │
    │◄──────────────────────────────── Generator logs
    │                                  │
    │                                  ├─► Commit images to GitHub
    │                                  └─► Commit metrics to GitHub
    │                                  │
    │ 8. Shutdown on completion         │
    ├──────────────────────────────► shutdown -h now
    │                                  
```

## Output Structure

### Generated Images Directory
```
generated_images/
├── .git/
├── worker0_image0_20240114_123456.png
├── worker0_image1_20240114_123512.png
├── worker1_image0_20240114_123458.png
├── ...
└── generation_status_20240114_124530.json
```

### Resource Metrics Directory
```
resource_metrics/
├── metrics_20240114_123500.json
├── metrics_20240114_123600.json
├── metrics_20240114_123700.json
└── resource_timeseries.csv
```

## Troubleshooting

### SSH Connection Issues

Make sure you can connect manually:
```bash
ssh -p 12345 root@1.2.3.4
```

### CUDA Out of Memory

Reduce number of processes:
```json
{
  "num_processes": 2
}
```

### Git Authentication

For private repositories, setup SSH keys or use personal access token:
```bash
# On Vast.ai instance
git config --global credential.helper store
```

### Process Crashes

Check the logs on the remote instance:
```bash
ssh -p 12345 root@1.2.3.4 "cat /workspace/generator.log"
```

### Model Download Issues

The first run downloads the Flux1 Schnell model (~20GB). Ensure:
- Sufficient disk space (50GB+ recommended)
- Good internet connection
- HuggingFace access token if model is gated

## Cost Optimization

1. **Use Auto-Shutdown**: Prevents unnecessary charges
2. **Test with Few Prompts**: Start with 5-10 prompts to verify setup
3. **Choose Right GPU**: RTX 3090 offers good performance/price ratio
4. **Monitor Prices**: Vast.ai prices vary by demand

## Security Considerations

1. **SSH Keys**: Use SSH keys instead of passwords
2. **Git Credentials**: Use access tokens, not passwords
3. **Private Repos**: Keep outputs in private repositories if sensitive
4. **Clean Up**: Auto-shutdown removes instance after completion

## Performance Tips

1. **Batch Size**: Flux1 Schnell is optimized for batch_size=1
2. **Multiple Processes**: Use 1 process per GPU
3. **Inference Steps**: Schnell works well with 4 steps (default)
4. **Image Size**: Default 1024x1024, can be adjusted in generator

## Example Session

```bash
# 1. Create prompts
cat > my_prompts.txt << EOF
A beautiful sunset over mountains
A cyberpunk city at night
A peaceful zen garden
EOF

# 2. Setup config
cat > my_config.json << EOF
{
  "git_repo_url": "https://github.com/myuser/flux-outputs.git",
  "git_user_name": "My Name",
  "git_user_email": "me@example.com",
  "num_processes": 2
}
EOF

# 3. Run orchestrator
python3 vastai_flux_orchestrator.py \
  "ssh -p 41234 root@ssh5.vast.ai" \
  my_prompts.txt \
  --config my_config.json

# Output:
# 2024-01-14 12:00:00 - INFO - Starting Vast.ai Flux1 Schnell orchestration...
# 2024-01-14 12:00:05 - INFO - Setting up workspace...
# 2024-01-14 12:00:10 - INFO - Installing required packages...
# 2024-01-14 12:05:30 - INFO - Package installation complete
# 2024-01-14 12:05:35 - INFO - Starting resource monitor...
# 2024-01-14 12:05:40 - INFO - Starting generation process...
# 2024-01-14 12:10:20 - INFO - Generated image 1: worker0_image0...
# ...
# 2024-01-14 12:45:00 - INFO - Generation completed successfully
# 2024-01-14 12:45:05 - INFO - Shutting down Vast.ai instance...
```

## License

MIT License - Same as parent project

## Contributing

Contributions welcome! Please ensure:
- Code follows existing style
- Add tests for new features
- Update documentation

## See Also

- [Flux1 Model Card](https://huggingface.co/black-forest-labs/FLUX.1-schnell)
- [Vast.ai Documentation](https://vast.ai/docs/)
- [Diffusers Library](https://github.com/huggingface/diffusers)
