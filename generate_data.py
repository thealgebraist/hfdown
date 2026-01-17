import os
import json

base_dir = "extreme_test_data"
files_per_size = 5
files = []

os.makedirs(f"{base_dir}/test/stress/resolve/main", exist_ok=True)

for size_k in range(1, 65):
    for i in range(1, files_per_size + 1):
        name = f"file_{size_k}k_{i}.bin"
        path = f"{base_dir}/test/stress/resolve/main/{name}"
        with open(path, "wb") as f:
            f.write(b"\0" * (size_k * 1024))
        files.append({"type": "file", "path": f"test/stress/resolve/main/{name}", "size": size_k * 1024})

with open(f"{base_dir}/api/models/test/stress/tree/main?recursive=true", "w") as f:
    json.dump(files, f)
