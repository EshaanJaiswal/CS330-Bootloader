# UEFI Bootloader Makefile
# Uses gnu-efi to build a PE32+ .efi executable

CC = x86_64-linux-gnu-gcc
CFLAGS = -I/usr/include/efi -I/usr/include/efi/x86_64 -I/usr/include/efi/protocol \
         -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -Wextra \
         -DEFI_FUNCTION_WRAPPER
LDFLAGS = -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds \
          -shared -Bsymbolic /usr/lib/crt0-efi-x86_64.o \
          -L/usr/lib -lgnuefi -lefi

TARGET = BOOTX64.EFI
SRC = src/main.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)