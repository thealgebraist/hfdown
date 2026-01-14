# Vast.ai Flux1 Schnell - Quick Start Guide

This guide will walk you through using the Vast.ai Flux1 Schnell orchestration system from start to finish.

## Step 1: Setup (5 minutes)

Run the setup script to install dependencies and create config files:

```bash
./setup_vastai.sh
```

This will:
- Install required Python packages (psutil, gitpython)
- Create `vastai_config.json` from the example
- Create `prompts.txt` from the example

## Step 2: Configure (2 minutes)

Edit `vastai_config.json` with your settings:

```json
{
  "git_repo_url": "https://github.com/yourusername/flux-outputs.git",
  "git_user_name": "Your Name",
  "git_user_email": "your.email@example.com",
  "num_processes": 4,
  "auto_shutdown": true
}
```

**Important:** Replace the git settings with your own repository if you want to automatically commit outputs.

## Step 3: Create Your Prompts (2 minutes)

Edit `prompts.txt` with your desired prompts, one per line:

```text
A beautiful mountain landscape at sunset
A futuristic cyberpunk city
A serene zen garden with cherry blossoms
An astronaut floating in space
```

You can add as many prompts as you want - the system will distribute them across multiple processes.

## Step 4: Rent a Vast.ai GPU Instance (5 minutes)

1. Go to https://vast.ai/ and log in
2. Click "Search" to find available GPU instances
3. Filter by:
   - **GPU**: RTX 3090, RTX 4090, or A6000 recommended
   - **VRAM**: At least 24GB
   - **Disk Space**: At least 50GB
4. Click "Rent" on your chosen instance
5. Wait for instance to start
6. Copy the SSH command (will look like: `ssh -p 41234 root@ssh5.vast.ai`)

## Step 5: Run the Orchestrator

Run the orchestrator with your SSH command and prompts:

```bash
python3 vastai_flux_orchestrator.py \
  "ssh -p 41234 root@ssh5.vast.ai" \
  prompts.txt \
  --config vastai_config.json
```

## What Happens Next

The orchestrator will automatically:

1. **Connect** to your Vast.ai instance (5 seconds)
   ```
   Testing SSH connection...
   SSH connection verified
   ```

2. **Check disk space** (5 seconds)
   ```
   Checking disk space...
   Available disk space: 120.5 GB
   ```

3. **Install packages** (3-5 minutes)
   ```
   Installing required packages...
   pip install torch torchvision diffusers transformers...
   Package installation complete
   ```

4. **Upload scripts and prompts** (10 seconds)
   ```
   Uploading scripts to remote instance...
   Uploading prompts from prompts.txt...
   ```

5. **Start resource monitor** (5 seconds)
   ```
   Starting resource monitor...
   ```

6. **Start generation** (varies by number of prompts)
   ```
   Starting generation process...
   Worker 0 starting with 3 prompts
   Worker 1 starting with 3 prompts
   Worker 2 starting with 2 prompts
   Worker 3 starting with 2 prompts
   ```

7. **Monitor progress** (real-time updates)
   ```
   Generated image 1: worker0_image0_20240114_123456.png
   Generated image 2: worker1_image0_20240114_123458.png
   ...
   ```

8. **Commit to GitHub** (every 5 images)
   ```
   Committed changes: Generated 5 images
   Pushed changes to remote
   ```

9. **Shutdown** (when complete)
   ```
   Generation completed successfully
   Shutting down Vast.ai instance...
   ```

## Expected Timing

For a typical setup with 10 prompts on an RTX 3090:

- Setup and package installation: **5 minutes**
- Model download (first time only): **5-10 minutes**
- Image generation: **~30 seconds per image**
- Total for 10 images: **15-20 minutes**

## Viewing Your Results

If you configured a git repository, your outputs will be automatically committed and pushed. You can view them on GitHub:

```
https://github.com/yourusername/flux-outputs/tree/main/generated_images
```

If you didn't configure git, you can manually download the results after generation:

```bash
scp -P 41234 root@ssh5.vast.ai:/workspace/generated_images/* ./my_outputs/
```

## Monitoring Resource Usage

Resource metrics are also saved and committed (if git is configured):

```
https://github.com/yourusername/flux-outputs/tree/main/resource_metrics
```

You can analyze the CSV file to see GPU, CPU, and memory usage over time.

## Troubleshooting

### Connection Issues

If you can't connect to Vast.ai:

```bash
# Test connection manually
ssh -p 41234 root@ssh5.vast.ai

# If this fails, check:
# - Instance is running (not stopped)
# - Port number is correct
# - Host address is correct
```

### Out of Memory

If you get CUDA out of memory errors:

1. Reduce `num_processes` in `vastai_config.json`:
   ```json
   {
     "num_processes": 2
   }
   ```

2. Or choose a GPU with more VRAM (24GB+ recommended)

### Slow Generation

If generation is very slow:

- Check you're using a GPU instance (not CPU)
- Check GPU utilization in resource metrics
- Consider upgrading to a faster GPU (RTX 4090 > RTX 3090 > RTX 3080)

### Git Push Fails

If automatic git push fails:

1. Check git credentials are set up on your local machine
2. Make sure repository URL is correct
3. Verify you have push permissions

You can disable auto-push and manually commit later:

```json
{
  "git_repo_url": null
}
```

## Advanced Usage

### Running Without Auto-Shutdown

To keep the instance running after generation:

```json
{
  "auto_shutdown": false
}
```

Then manually shutdown later from Vast.ai dashboard.

### Custom Inference Steps

For higher quality (but slower) generation:

1. SSH into the instance:
   ```bash
   ssh -p 41234 root@ssh5.vast.ai
   ```

2. Run generator manually:
   ```bash
   cd /workspace
   python3 flux_generator.py \
     --prompts prompts.txt \
     --output-dir generated_images \
     --num-processes 4 \
     --num-inference-steps 8 \
     --git-auto-commit
   ```

### Batch Processing

To generate multiple batches:

1. Create multiple prompt files: `batch1.txt`, `batch2.txt`, etc.
2. Run orchestrator multiple times with different prompts
3. Or combine all prompts into one file for a single run

## Cost Estimation

Typical costs on Vast.ai (as of 2024):

- **RTX 3090**: $0.20-0.40/hour
- **RTX 4090**: $0.40-0.60/hour
- **A6000**: $0.50-0.80/hour

For 10 images with setup (~20 minutes):

- **Cost**: $0.07-0.20 depending on GPU chosen

The auto-shutdown feature ensures you only pay for what you use!

## Tips for Best Results

1. **Start Small**: Test with 2-3 prompts first to verify everything works
2. **Good Prompts**: Be descriptive and specific in your prompts
3. **Monitor Resources**: Check resource metrics to ensure GPU is being utilized
4. **Batch Similar Prompts**: Group similar prompts together for efficiency
5. **Use Auto-Shutdown**: Save money by shutting down when done

## Getting Help

If you encounter issues:

1. Check the logs on the remote instance:
   ```bash
   ssh -p 41234 root@ssh5.vast.ai "cat /workspace/generator.log"
   ```

2. Check the orchestrator output for error messages

3. Review the documentation in `VASTAI_FLUX_README.md`

4. Open an issue on GitHub with:
   - The command you ran
   - Error messages
   - Relevant log snippets

## Next Steps

Once you have the basic workflow working:

1. Experiment with different prompts
2. Try different GPU types to compare speed/cost
3. Set up your own git repository for automatic backups
4. Analyze resource metrics to optimize performance
5. Generate larger batches (100+ images)

Happy generating! 🎨✨
