CC       := gcc
OBJCOPY  := objcopy
ARCH     := x86_64

EFI_INC  := /usr/include/efi
EFI_LIB  := /usr/lib
CRT0     := $(EFI_LIB)/crt0-efi-$(ARCH).o
LDS      := $(EFI_LIB)/elf_$(ARCH)_efi.lds

CFLAGS := -std=gnu11 -fshort-wchar -fPIC -ffreestanding -fno-stack-protector \
          -fno-builtin -mno-red-zone -Wall -Wextra \
          -DGNU_EFI_USE_MS_ABI \
          -I$(EFI_INC) -I$(EFI_INC)/$(ARCH)

LDFLAGS := -nostdlib -shared -Wl,-T,$(LDS) -Wl,-soname,loader.so -Wl,-Bsymbolic
LIBS    := -L$(EFI_LIB) -lgnuefi -lefi

ESP  := esp
OVMF := /usr/share/ovmf/OVMF.fd

OBJS := build/main.o build/console.o build/file.o build/linux_boot.o

.PHONY: all rootfs run clean

all: build/BOOTX64.EFI

# Build a full Alpine userspace (login prompt + apk) into the ESP.
rootfs:
	@sh scripts/make_rootfs.sh

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/loader.so: $(OBJS) $(CRT0)
	$(CC) $(LDFLAGS) -o $@ $(CRT0) $(OBJS) $(LIBS)

build/BOOTX64.EFI: build/loader.so
	$(OBJCOPY) -I elf64-x86-64 -O pei-x86-64 --subsystem=efi-app \
		-j .text -j .sdata -j .data -j .rodata -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc \
		$< $@

run: all rootfs
	@mkdir -p $(ESP)/EFI/BOOT
	cp build/BOOTX64.EFI $(ESP)/EFI/BOOT/BOOTX64.EFI
	qemu-system-x86_64 \
		-bios $(OVMF) \
		-drive file=fat:rw:$(ESP),format=raw \
		-m 1024M -no-reboot \
		-netdev user,id=net0 \
		-device virtio-net-pci,netdev=net0 \
		-serial stdio

clean:
	rm -rf build $(ESP)/EFI/BOOT/BOOTX64.EFI
