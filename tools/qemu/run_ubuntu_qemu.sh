#!/usr/bin/env bash
set -euo pipefail

# Simple helper to boot an Ubuntu cloud image under QEMU with a 9p share of the repo
# Usage: ./run_ubuntu_qemu.sh [image-url]

IMAGE_URL=${1:-https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img}
CACHE_DIR=${HOME}/.cache/hfdown/qemu
mkdir -p "$CACHE_DIR"
IMG_NAME=$(basename "$IMAGE_URL")
IMG_PATH="$CACHE_DIR/$IMG_NAME"

if [ ! -f "$IMG_PATH" ]; then
  echo "Downloading Ubuntu cloud image to $IMG_PATH..."
  curl -L -o "$IMG_PATH" "$IMAGE_URL"
fi

WORKDIR=$(pwd)
CI_ISO="$WORKDIR/tools/qemu/cloud-init.iso"

echo "Generating cloud-init ISO ($CI_ISO)..."
tmpdir=$(mktemp -d)
cp tools/qemu/user-data "$tmpdir/"
cp tools/qemu/meta-data "$tmpdir/"
genisoimage -output "$CI_ISO" -volid cidata -joliet -rock "$tmpdir/user-data" "$tmpdir/meta-data" >/dev/null
rm -rf "$tmpdir"

echo "Starting QEMU. SSH will be forwarded to localhost:2222. Repo will be available inside VM as 9p mount 'hostshare'."

qemu-system-x86_64 \
  -m 4096 -smp 2 \
  -drive if=virtio,file="$IMG_PATH",format=qcow2 \
  -cdrom "$CI_ISO" \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  -fsdev local,id=fsdev0,path="$WORKDIR",security_model=passthrough \
  -device virtio-9p-pci,fsdev=fsdev0,mount_tag=hostshare \
  -nographic -serial mon:stdio

echo "QEMU exited"
