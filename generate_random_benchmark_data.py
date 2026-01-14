import os
import random
import json
import argparse

def generate_file(path, size_mb, random_data=False):
    chunk_size = 1024 * 1024 # 1MB chunks
    with open(path, "wb") as f:
        if random_data:
            remaining = int(size_mb * 1024 * 1024)
            while remaining > 0:
                to_write = min(remaining, chunk_size)
                f.write(os.urandom(to_write))
                remaining -= to_write
        else:
            f.truncate(int(size_mb * 1024 * 1024))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", default="test_server_data")
    parser.add_argument("--model-name", default="test/random-model")
    parser.add_argument("--num-small", type=int, default=50)
    parser.add_argument("--small-size-kb", type=float, default=1.0)
    parser.add_argument("--large-sizes-mb", type=float, nargs="+", default=[])
    parser.add_argument("--random-large", action="store_true")
    args = parser.parse_args()

    model_dir = os.path.join(args.out_dir, args.model_name)
    os.makedirs(model_dir, exist_ok=True)

    files_info = []

    # Large files
    for i, size_mb in enumerate(args.large_sizes_mb):
        filename = f"large_file_{i}_{int(size_mb)}mb.bin"
        print(f"Generating {filename} ({size_mb} MB) with random bytes: {args.random_large}...")
        generate_file(os.path.join(model_dir, filename), size_mb, random_data=args.random_large)
        files_info.append({"path": filename, "size": int(size_mb * 1024 * 1024), "type": "file"})

    # Small files
    print(f"Generating {args.num_small} small files of ~{args.small_size_kb} KB...")
    for i in range(args.num_small):
        filename = f"small_file_{i}.txt"
        size_kb = args.small_size_kb * random.uniform(0.8, 1.2)
        generate_file(os.path.join(model_dir, filename), size_kb / 1024.0, random_data=True)
        files_info.append({"path": filename, "size": int(size_kb * 1024), "type": "file"})

    # Generate the API JSON response
    # Path: test_server_data/api/models/<model_id>
    api_dir = os.path.join(args.out_dir, "api", "models", args.model_name)
    os.makedirs(api_dir, exist_ok=True)
    
    # Simple tree view
    tree_path = os.path.join(api_dir, "tree", "main")
    os.makedirs(os.path.dirname(tree_path), exist_ok=True)
    
    with open(tree_path, "w") as f:
        json.dump(files_info, f)

    print(f"Generated {len(files_info)} files in {model_dir}")

if __name__ == "__main__":
    main()
