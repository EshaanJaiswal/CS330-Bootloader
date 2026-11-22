# Minimal gnu-efi build. Each flag is explained in the README/lesson notes.
CC       := gcc
OBJCOPY  := objcopy
ARCH     := x86_64

EFI_INC  := /usr/include/efi
EFI_LIB  := /usr/lib
CRT0     := $(EFI_LIB)/crt0-efi-$(ARCH).o
LDS      := $(EFI_LIB)/elf_$(ARCH)_efi.lds

# UEFI calls C functions with the Microsoft x64 ABI, and wide (16-bit) strings.
# freestanding + no stack protector: there is no libc underneath us.
CFLAGS := -std=gnu11 -fshort-wchar -fPIC -ffreestanding -fno-stack-protector \
          -fno-builtin -mno-red-zone -Wall -Wextra \
          -DGNU_EFI_USE_MS_ABI \
          -I$(EFI_INC) -I$(EFI_INC)/$(ARCH)

# -Bsymbolic keeps references to ImageBase local to this image (needed by crt0).
LDFLAGS := -nostdlib -shared -Wl,-T,$(LDS) -Wl,-soname,loader.so -Wl,-Bsymbolic
LIBS    := -L$(EFI_LIB) -lgnuefi -lefi

ESP  := esp
OVMF := /usr/share/ovmf/OVMF.fd

.PHONY: all run clean

all: build/BOOTX64.EFI

# 1. Compile C -> relocatable ELF object (still a normal .o).
build/main.o: src/main.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# 2. Link object + EFI startup code into a shared ELF using the EFI linker script.
build/loader.so: build/main.o $(CRT0)
	$(CC) $(LDFLAGS) -o $@ $(CRT0) $< $(LIBS)

# 3. Convert ELF -> PE32+ executable, which is what firmware actually loads.
# Ubuntu binutils has no "efi-app-x86_64" alias, so we name the PE target and
# set the PE subsystem to 10 ("efi-app") explicitly.
build/BOOTX64.EFI: build/loader.so
	$(OBJCOPY) -I elf64-x86-64 -O pei-x86-64 --subsystem=efi-app \
		-j .text -j .sdata -j .data -j .rodata -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc \
		$< $@

# Put the binary where firmware expects it, then boot it in QEMU.
# QEMU's "fat:" drive maps a host directory as a FAT volume, so no mtools needed.
run: all
	@mkdir -p $(ESP)/EFI/BOOT
	cp build/BOOTX64.EFI $(ESP)/EFI/BOOT/BOOTX64.EFI
	qemu-system-x86_64 \
		-bios $(OVMF) \
		-drive file=fat:rw:$(ESP),format=raw \
		-m 256M -net none -no-reboot \
		-display none -serial stdio

clean:
	rm -rf build $(ESP)/EFI/BOOT/BOOTX64.EFI
