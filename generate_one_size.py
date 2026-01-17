import os
import json
base_dir = "extreme_test_data"
files = []
os.makedirs(f"{base_dir}/test/stress/resolve/main", exist_ok=True)
for i in range(1, 10 + 1):
    name = f"file_64k_{i}.bin"
    # PATH in JSON should be relative to the 'resolve/main' logic in HF
    # hf_client.cpp expects filename relative to model root
    rel_path = f"file_64k_{i}.bin"
    full_path = f"{base_dir}/test/stress/resolve/main/{name}"
    with open(full_path, "wb") as f:
        f.write(b"\0" * (64 * 1024))
    files.append({"type": "file", "path": rel_path, "size": 64 * 1024})
with open(f"{base_dir}/api/models/test/stress/tree/main", "w") as f:
    json.dump(files, f)
