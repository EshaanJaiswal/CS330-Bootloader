#!/bin/bash
# Create a FAT32 disk image with EFI System Partition layout

set -e

IMG=boot.img
EFI_DIR=EFI/BOOT

# Clean up any existing image
rm -f $IMG

# Create 64MB empty image
dd if=/dev/zero of=$IMG bs=1M count=64

# Format as FAT32
mkfs.fat -F 32 $IMG

# Create EFI directory structure
mmd -i $IMG ::/$EFI_DIR

# Copy bootloader
mcopy -i $IMG BOOTX64.EFI ::/$EFI_DIR/

echo "Created $IMG with EFI/BOOT/BOOTX64.EFI"
ls -la $IMG