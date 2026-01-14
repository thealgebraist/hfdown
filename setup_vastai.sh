#!/bin/bash
# Quick setup script for Vast.ai Flux1 Schnell orchestration

set -e

echo "============================================"
echo "Vast.ai Flux1 Schnell Orchestration Setup"
echo "============================================"
echo ""

# Check for Python 3
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 is required but not found"
    exit 1
fi

# Install local requirements
echo "Installing local requirements..."
pip3 install -r requirements-vastai.txt || {
    echo "Warning: Could not install requirements. You may need to install psutil and gitpython manually."
}

# Create config from example if it doesn't exist
if [ ! -f vastai_config.json ]; then
    echo ""
    echo "Creating vastai_config.json from example..."
    cp vastai_config.example.json vastai_config.json
    echo "✓ Created vastai_config.json"
    echo ""
    echo "IMPORTANT: Please edit vastai_config.json with your settings:"
    echo "  - git_repo_url: Your GitHub repository URL"
    echo "  - git_user_name: Your name for git commits"
    echo "  - git_user_email: Your email for git commits"
    echo "  - num_processes: Number of parallel processes (default: 4)"
fi

# Create prompts from example if it doesn't exist
if [ ! -f prompts.txt ]; then
    echo ""
    echo "Creating prompts.txt from example..."
    cp prompts.example.txt prompts.txt
    echo "✓ Created prompts.txt"
    echo ""
    echo "Edit prompts.txt to add your own prompts (one per line)"
fi

echo ""
echo "============================================"
echo "Setup Complete!"
echo "============================================"
echo ""
echo "Next steps:"
echo "  1. Edit vastai_config.json with your settings"
echo "  2. Edit prompts.txt with your prompts"
echo "  3. Rent a Vast.ai GPU instance"
echo "  4. Run: python3 vastai_flux_orchestrator.py 'ssh -p PORT root@HOST' prompts.txt --config vastai_config.json"
echo ""
echo "See VASTAI_FLUX_README.md for detailed documentation"
echo ""
