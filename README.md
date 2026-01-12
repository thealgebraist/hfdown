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
3. You have internet connectivity

## Future Enhancements

- [ ] Parallel file downloads
- [ ] Resume interrupted downloads
- [ ] Model verification (checksums)
- [ ] Custom branch/revision support
- [ ] Cache management
- [ ] Compression support

## See Also

- [HuggingFace Hub Documentation](https://huggingface.co/docs/hub/index)
- [HuggingFace Models](https://huggingface.co/models)
