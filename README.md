# HuggingFace Model Downloader (hfdown)

A high-performance HuggingFace model downloader written in pure C++23 with minimal dependencies.

## Features

- ✨ Modern C++23 implementation
- 🚀 Fast parallel downloads with progress tracking
- 📊 Real-time download speed and progress display
- 🔒 Support for private models via HuggingFace tokens
- 🎯 Download entire models or specific files
- 🛠️ Simple command-line interface
- ⚡ Built-in JSON parser (no external JSON library needed)

## Requirements

- **C++23** compatible compiler (GCC 13+, Clang 16+, or MSVC 2022+)
- **CMake** 3.25 or higher
- **libcurl** development libraries

### Installing Dependencies

#### macOS
```bash
brew install cmake curl
```

#### Ubuntu/Debian
```bash
sudo apt update
sudo apt install cmake libcurl4-openssl-dev build-essential
```

#### Fedora/RHEL
```bash
sudo dnf install cmake libcurl-devel gcc-c++
```

## Building

```bash
# Clone the repository
cd /path/to/hfdown

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make

# Optionally install system-wide
sudo make install
```

## Usage

### Get Model Information

Get details about a model including file list and sizes:

```bash
./hfdown info <model-id>

# Example
./hfdown info gpt2
./hfdown info microsoft/phi-2
```

### Download Entire Model

Download all files from a model:

```bash
./hfdown download <model-id> [output-directory]

# Examples
./hfdown download gpt2
./hfdown download gpt2 ./models/gpt2
./hfdown download microsoft/phi-2 ./phi2
```

### Download Specific File

Download a single file from a model:

```bash
./hfdown file <model-id> <filename>

# Examples
./hfdown file gpt2 config.json
./hfdown file gpt2 pytorch_model.bin
```

### Using Authentication Token

For private models or to avoid rate limits:

```bash
# Set environment variable
export HF_TOKEN="your_huggingface_token_here"
./hfdown download private-model/my-model

# Or pass token via command line
./hfdown download private-model/my-model --token "your_token_here"
```

## Examples

```bash
# Get information about GPT-2
./hfdown info gpt2

# Download GPT-2 to current directory
./hfdown download gpt2

# Download Microsoft Phi-2 to specific directory
./hfdown download microsoft/phi-2 ./models/phi2

# Download only config file
./hfdown file gpt2 config.json

# Download with authentication
export HF_TOKEN="hf_..."
./hfdown download meta-llama/Llama-2-7b-hf ./llama2
```

## Project Structure

```
hfdown/
├── CMakeLists.txt           # Build configuration
├── README.md                # This file
├── include/
│   ├── json.hpp            # Minimal JSON parser
│   ├── http_client.hpp     # HTTP client interface
│   └── hf_client.hpp       # HuggingFace API client
└── src/
    ├── main.cpp            # CLI application
    ├── http_client.cpp     # HTTP implementation
    └── hf_client.cpp       # HuggingFace implementation
```

## Architecture

### Core Components

1. **JSON Parser** (`json.hpp`): Lightweight, header-only JSON parser built specifically for HuggingFace API responses
2. **HTTP Client** (`http_client.{hpp,cpp}`): CURL wrapper with progress callbacks and modern C++ error handling
3. **HuggingFace Client** (`hf_client.{hpp,cpp}`): High-level API for interacting with HuggingFace Hub
4. **CLI** (`main.cpp`): User-friendly command-line interface

### C++23 Features Used

- `std::expected` for error handling without exceptions
- `std::format` for string formatting
- `std::filesystem` for path manipulation
- Concepts for template constraints
- Modern move semantics and RAII

## Performance

The downloader uses libcurl's efficient streaming capabilities and provides real-time progress updates showing:
- Download percentage
- Downloaded/total size in MB
- Current download speed in MB/s

## Limitations

**HuggingFace Cache Compatibility**: This tool downloads models to a simple directory structure. Files are NOT automatically compatible with the HuggingFace Hub cache format (`.cache/huggingface/hub/models--{org}--{model}/blobs/`). If you need cache compatibility with the official `huggingface_hub` library, use the official Python tools or manually copy files to the cache structure.

## Error Handling

All operations use `std::expected` for clean error propagation. Errors include:
- Network failures
- HTTP errors (404, 401, etc.)
- File system errors
- JSON parsing errors

## Contributing

Contributions are welcome! This is a pure C++23 project with minimal dependencies.

## License

MIT License - feel free to use in your projects.

## Troubleshooting

### Compiler Errors

Make sure you have a C++23-compatible compiler:
```bash
g++ --version  # Should be 13+
clang++ --version  # Should be 16+
```

### Missing libcurl

If CMake can't find curl:
```bash
# macOS
brew install curl

# Ubuntu/Debian
sudo apt install libcurl4-openssl-dev

# Fedora
sudo dnf install libcurl-devel
```

### Runtime Errors

If you get "Model not found" errors, ensure:
1. Model ID is correct (use the exact ID from huggingface.co)
2. Model is public or you have provided a valid token
3. YouHuggingFace cache format compatibility (`.cache/huggingface/hub` structure)
- [ ]  have internet connectivity

## Rsync-like Transfer (New!)

HFDown now supports rsync-like incremental transfers that work seamlessly with Vast.ai GPU instances!

### Features

- **Incremental Sync**: Only downloads files that are new or have changed (compares size and SHA256 checksums)
- **Resume Support**: Built on top of the existing HTTP resume capability
- **Vast.ai Integration**: Direct sync to remote GPU instances via SSH/SCP
- **Dry-run Mode**: Preview what would be synced without downloading
- **Smart Comparison**: Uses Git LFS object IDs for accurate file comparison

### Usage

#### Sync to Local Directory

Incrementally sync a model to a local directory (only downloads new/changed files):

```bash
# First sync - downloads all files
./hfdown rsync-sync gpt2 ./models/gpt2

# Subsequent syncs - only downloads what changed
./hfdown rsync-sync gpt2 ./models/gpt2

# Dry-run to see what would be synced
./hfdown rsync-sync gpt2 ./models/gpt2 --dry-run --verbose

# Faster sync without checksum verification (less safe)
./hfdown rsync-sync gpt2 ./models/gpt2 --no-checksum
```

#### Sync to Vast.ai Instance

Download models directly to your Vast.ai GPU instance:

```bash
# Get your Vast.ai SSH command from the web interface
# Example: "ssh -p 12345 root@1.2.3.4"

# Sync model to remote instance
./hfdown rsync-to-vast gpt2 'ssh -p 12345 root@1.2.3.4' /workspace/models/gpt2

# With authentication key
./hfdown rsync-to-vast gpt2 'ssh -p 12345 -i ~/.ssh/vast_key root@1.2.3.4' /workspace/models

# Dry-run to test connection
./hfdown rsync-to-vast gpt2 'ssh -p 12345 root@1.2.3.4' /workspace/models --dry-run --verbose
```

### How It Works

1. **Initial Download**: On first sync, downloads all model files
2. **Subsequent Syncs**: 
   - Compares local files against remote model info
   - Checks file size and SHA256 checksums (Git LFS OIDs)
   - Only downloads files that don't exist or have mismatched checksums
3. **Remote Sync**: Downloads to temp directory, then transfers via SCP

### Comparison with rsync

| Feature | hfdown rsync | traditional rsync |
|---------|--------------|-------------------|
| Incremental sync | ✅ | ✅ |
| Resume downloads | ✅ | ✅ |
| Checksum verification | ✅ (SHA256) | ✅ (MD5/SHA) |
| Remote sync via SSH | ✅ | ✅ |
| Direct from HuggingFace | ✅ | ❌ |
| Delta transfers | ❌ | ✅ |

### Options

- `--verbose`: Show detailed progress for each file
- `--dry-run`: Preview what would be synced without downloading
- `--no-checksum`: Skip checksum verification (faster but less safe, only compares sizes)
- `--token <token>`: HuggingFace API token for private models

## Future Enhancements

- [x] Rsync-like incremental downloads
- [ ] Parallel file downloads
- [ ] Custom branch/revision support
- [ ] Cache management
- [ ] Compression support

## See Also

- [HuggingFace Hub Documentation](https://huggingface.co/docs/hub/index)
- [HuggingFace Models](https://huggingface.co/models)
- [Vast.ai Documentation](https://vast.ai/docs/)
