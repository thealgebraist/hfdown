import os
import random
import argparse
import json

def generate_random_file(path, size):
    with open(path, 'wb') as f:
        f.write(os.urandom(size))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default="test_server_data")
    parser.add_argument("--small", type=int, default=50)
    parser.add_argument("--large", type=int, default=5)
    args = parser.parse_args()

    model_id = "test/random-model"
    model_dir = os.path.join(args.dir, model_id)
    os.makedirs(model_dir, exist_ok=True)

    files_info = []

    # Small files (1KB - 100KB)
    for i in range(args.small):
        filename = f"small_file_{i}.txt"
        size = random.randint(1024, 100 * 1024)
        generate_random_file(os.path.join(model_dir, filename), size)
        files_info.append({"path": filename, "type": "file", "size": size, "oid": f"oid_small_{i}"})

    # Large files (10MB - 100MB)
    for i in range(args.large):
        filename = f"large_weight_{i}.bin"
        size = random.randint(10 * 1024 * 1024, 100 * 1024 * 1024)
        generate_random_file(os.path.join(model_dir, filename), size)
        files_info.append({"path": filename, "type": "file", "size": size, "oid": f"oid_large_{i}"})

    # Save API metadata
    api_dir = os.path.join(args.dir, "api", "models", model_id, "tree")
    os.makedirs(api_dir, exist_ok=True)
    with open(os.path.join(api_dir, "main"), "w") as f:
        json.dump(files_info, f)

    print(f"Generated model {model_id} with {len(files_info)} files in {args.dir}")

if __name__ == "__main__":
    main()