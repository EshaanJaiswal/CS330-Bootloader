# CS330 Project: UEFI Bootloader for Alpine Linux

A custom UEFI bootloader written in C using `gnu-efi` that boots Alpine Linux from an EFI System Partition. Built as a semester project for **CS330: Operating Systems** to demonstrate understanding of the boot process, memory management, hardware abstraction, and the kernel interface.

## Overview

This project implements a minimal but fully functional UEFI bootloader capable of:

- Parsing a configuration file from the EFI System Partition
- Loading a Linux kernel (`vmlinuz`) and initial ramdisk (`initramfs`) into memory
- Setting up the Linux Boot Protocol (`boot_params`) structure
- Managing the UEFI-to-kernel memory map handoff (`ExitBootServices`)
- Transferring control to the Linux kernel

The bootloader also includes additional features — a graphical framebuffer console (via UEFI GOP), an interactive kernel command-line editor, and a system information screen — to demonstrate deeper engagement with UEFI firmware services.

## Motivation

The standard OS course covers process scheduling, virtual memory, file systems, and concurrency — all concepts that assume the kernel is already running. This project explores **what happens before the kernel**: how firmware discovers hardware, how a bootloader loads the kernel into memory, and how control is transferred from firmware to the operating system. This is the part of the boot process that tools like GRUB handle transparently, and understanding it provides a complete picture of the OS lifecycle from power-on to userspace.

## Target Configuration

| Component | Detail |
|-----------|--------|
| **Firmware** | UEFI (64-bit, x86_64) |
| **Bootloader language** | C (with `gnu-efi`) |
| **Target OS** | Alpine Linux (x86_64, LTS kernel) |
| **Primary test platform** | QEMU + OVMF |
| **Secondary test platform** | Gigabyte NUC BSi7HAL (Intel i7-6500U) via USB boot |

## Build & Run

> **Prerequisites**: `gcc`, `gnu-efi`, `qemu-system-x86_64`, `OVMF.fd`, `mtools` (for FAT32 image creation)

```bash
# Build the bootloader
make

# Create the disk image and run in QEMU
make run
```

The QEMU invocation uses serial console output (`-serial stdio`) for kernel
message visibility. The boot menu currently starts a live Alpine userspace;
it does not install Alpine or persist changes to disk.

## USB Boot

Create a FAT32 USB partition, copy `build/BOOTX64.EFI` to
`EFI/BOOT/BOOTX64.EFI`, and copy these files to the partition root:

- `vmlinuz-virt`
- `alpine-rootfs.cpio.gz`

Boot the USB from a 64-bit UEFI firmware menu. A serial terminal receives
kernel output and the login prompt; a QEMU or EFI-capable framebuffer receives
the graphical Alpine console. This supports live boot only.

## Project Structure

```
CS330-Bootloader/
├── Makefile                      # Build system
├── PLAN.md                       # Formal project plan
├── AGENTS.md                     # AI usage disclosure
├── src/
│   ├── main.c                   # efi_main() entry, boot menu, main flow
│   ├── console.c / console.h    # Text I/O wrappers (SimpleTextOutput)
│   ├── file.c / file.h          # File loading from ESP (SimpleFileSystem)
│   ├── config.c / config.h      # Configuration file parser
│   ├── linux.c / linux.h        # Linux Boot Protocol implementation
│   ├── memory.c / memory.h      # Memory map handling, UEFI → E820 conversion
│   ├── gop.c / gop.h            # Graphics Output Protocol (framebuffer)
│   └── util.c / util.h          # String and utility helpers
├── boot.cfg                     # Boot configuration file
├── scripts/
│   ├── make_image.sh            # Create FAT32 ESP disk image
│   └── run_qemu.sh              # QEMU launch script
└── docs/
    └── boot-flow.md             # Detailed boot flow documentation
```

## References

- [UEFI Specification](https://uefi.org/specifications) — Protocol definitions (Boot Services, File System, GOP)
- [Linux x86 Boot Protocol](https://www.kernel.org/doc/html/latest/arch/x86/boot.html) — The bootloader ↔ kernel contract
- [OSDev Wiki: UEFI](https://wiki.osdev.org/UEFI) — Practical UEFI development guide
- [OSDev Wiki: GNU-EFI](https://wiki.osdev.org/GNU-EFI) — gnu-efi toolchain setup
- [OSTEP](https://pages.cs.wisc.edu/~remzi/OSTEP/) — Course textbook (Arpaci-Dusseau)

## License

This project is developed for academic purposes as part of CS330. See [AGENTS.md](AGENTS.md) for AI usage disclosure.
