# Project Plan: UEFI Bootloader for Alpine Linux

**Course**: CS330 — Operating Systems  
**Student**: Eshaan Ashish Jaiswal  
**Instructor**: Prof. Balagopal Komarath  
**Repository**: CS330-Bootloader

---

## 1. What & Why

Operating systems courses start at the kernel — scheduling, virtual memory, file systems all assume the OS is already running. But **how does the kernel get loaded in the first place?** This is a fundamental question that OSTEP doesn't cover, and implementing a bootloader answers it hands-on.

This project builds a UEFI bootloader from scratch in C that can boot Alpine Linux on x86_64. The bootloader loads the kernel and initial ramdisk from disk, sets up the information the kernel needs (memory layout, command line, display), and hands over control — essentially doing what GRUB does in its core boot path.

## 2. Objectives

1. Build a working bootloader from scratch that boots an unmodified Alpine Linux kernel.
2. Understand the full boot process — what happens between pressing the power button and the kernel taking over.
3. Apply OS concepts from the course (memory management, hardware abstraction, the kernel interface) in the pre-kernel environment where none of the usual OS infrastructure exists yet.
4. Add interactive features (boot menu, command-line editor, system info display) that go beyond a minimal loader.

## 3. Why This Project is Comparable to a Scheduler

| | xv6 / sched_ext Scheduler | This Bootloader |
|---|---|---|
| Starting point | Modify an existing codebase | Build from scratch — no skeleton code |
| Environment | Inside a running kernel with debugging, memory management, and I/O already working | Before the kernel exists — no malloc, no printf, no debugger |
| Core concepts | Scheduling (OSTEP Ch 7–10) | Memory layout (Ch 13–24), hardware abstraction, persistence (Ch 36–42) |
| Failure mode | Kernel catches faults, prints errors | Machine silently resets — no error messages |

The bootloader project is comparable because it requires building a complete program from an empty file, working without any OS support, and correctly implementing the contract between the bootloader and the Linux kernel.

## 4. Technical Approach

**Language**: C with the `gnu-efi` library — consistent with the course (OSTEP, xv6 are C-based), and UEFI's native interface is C-callable.

**How the bootloader works**, at a high level:

```
UEFI Firmware starts our bootloader
        │
        ▼
Show a boot menu, read a config file
        │
        ▼
Load the kernel and initramfs files from disk
        │
        ▼
Tell the kernel about the system (memory, display, command line)
        │
        ▼
Hand over control → Linux boots → Alpine login prompt
```

The critical challenge is the **handoff**: before jumping to the kernel, the bootloader must ask the firmware for the full system memory map, tell the firmware "I'm done with you" (`ExitBootServices`), and then jump to the kernel with no safety net. After that call, all firmware services disappear — no screen output, no disk access, no memory allocation. If anything is wrong, the machine resets silently.

## 5. Development Plan

### Phase 1: Setup & Core Bootloader (Days 1–4)

- Set up the build environment (QEMU, OVMF firmware, gnu-efi)
- Build a minimal UEFI application that prints to screen
- Implement console I/O and an interactive boot menu with a countdown timer
- Read a configuration file and kernel/initramfs files from the EFI System Partition

**Milestone**: Boot menu appears in QEMU, reads entries from a config file.

### Phase 2: Boot Alpine Linux (Days 4–7)

- Implement the Linux Boot Protocol — set up the data structures the kernel expects
- Load the kernel and initramfs into the correct memory locations
- Build the memory map, exit firmware services, and jump to the kernel

**Milestone**: Alpine Linux kernel boots to a login prompt in QEMU.

### Phase 3: Additional Features & Polish (Days 7–10)

- **Graphical console**: Use UEFI's Graphics Output Protocol to render the boot menu on a framebuffer instead of text mode
- **Command-line editor**: Let the user edit the kernel command line before booting (like GRUB's `e` key)
- **System info screen**: Display CPU, RAM, firmware version, and Secure Boot status
- Testing on real hardware (Gigabyte NUC via USB) if available

**Milestone**: Polished bootloader with interactive features, tested on QEMU (and optionally real hardware).

## 6. Testing

| Test | Method |
|------|--------|
| Bootloader loads and shows menu | QEMU + OVMF |
| Config file is parsed correctly | Entries from boot.cfg appear in menu |
| Alpine Linux boots successfully | Kernel messages on serial console, login prompt reached |
| Command-line editor works | Modified cmdline reflected in `/proc/cmdline` after boot |
| Real hardware (stretch goal) | Gigabyte NUC BSi7HAL, boot from USB flash drive |

**Primary test platform**: QEMU with OVMF (UEFI firmware for virtual machines) and serial console output.

## 7. How This Connects to OSTEP

| OSTEP Topic | Where It Shows Up |
|-------------|-------------------|
| **Address Spaces & Memory** (Ch 13–24) | The bootloader allocates physical memory for the kernel, builds the memory map that tells Linux which RAM is usable, and manages memory without any OS support. |
| **Abstraction** (Ch 2, 6) | UEFI protocols abstract the hardware (disk, display, keyboard). The bootloader uses these abstractions to implement boot policy. |
| **Persistence & File Systems** (Ch 36–42) | The bootloader reads files from a FAT32 file system on the EFI System Partition to find the kernel and configuration. |
| **The Kernel Interface** | The Linux Boot Protocol is essentially a contract — a data structure the bootloader fills in so the kernel knows about its environment. This is the same concept as a system call interface, but at the firmware/kernel boundary. |

## 8. References

1. UEFI Specification — https://uefi.org/specifications
2. Linux x86 Boot Protocol — https://www.kernel.org/doc/html/latest/arch/x86/boot.html
3. OSDev Wiki (UEFI, GNU-EFI) — https://wiki.osdev.org/UEFI
4. OSTEP — https://pages.cs.wisc.edu/~remzi/OSTEP/
