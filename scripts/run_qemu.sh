#!/bin/bash
# Launch QEMU with OVMF UEFI firmware and serial console

set -e

OVMF=/usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS=/usr/share/OVMF/OVMF_VARS_4M.fd

qemu-system-x86_64 \
    -machine q35 \
    -bios $OVMF \
    -drive file=boot.img,format=raw,if=virtio \
    -serial stdio \
    -m 512M \
    -net none