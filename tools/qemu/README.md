QEMU Ubuntu test harness
------------------------

This folder contains a small helper to boot an Ubuntu cloud image under QEMU, share the repository via 9p, and forward SSH to the host.

Quick steps:

1. Ensure `qemu-system-x86_64`, `genisoimage` and `curl` are installed on your host.
2. Run the helper:

```bash
./tools/qemu/run_ubuntu_qemu.sh
```

3. SSH into the VM once it finishes booting (password authentication enabled):

```bash
ssh -p 2222 ubuntu@localhost
# password: ubuntu
```

4. Mount the host repo inside the VM:

```bash
sudo mkdir -p /mnt/host
sudo mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
cd /mnt/host
python3 test_vastai_orchestration.py
```

Notes:
- On macOS, QEMU runs in user-space; `-enable-kvm` is not used.
- If your host QEMU binary is different, adjust the `qemu-system-x86_64` path in the script.
