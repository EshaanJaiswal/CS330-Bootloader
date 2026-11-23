#include <efi.h>
#include <efilib.h>
#include "console.h"
#include "file.h"
#include "linux_boot.h"

#define MAX_MENU_ENTRIES 3

typedef struct {
    CHAR16 *title;
    CHAR16 *description;
    CHAR8  *cmdline;
} BootEntry;

/*
 * The ESP contains a full Alpine minirootfs (built by scripts/make_rootfs.sh)
 * as alpine-rootfs.cpio.gz.  Stock initramfs-virt is only a bootstrap image
 * that expects a separate ISO/squashfs root, so it is not used here.
 * rdinit=/sbin/init starts BusyBox init, which spawns a getty on ttyS0.
 *
 * ttyS0 is last so it becomes /dev/console: kernel output and the login prompt
 * work normally on the serial terminal, while tty0 remains available for a
 * graphical terminal.
 */
static BootEntry menu_entries[MAX_MENU_ENTRIES] = {
    {
        L"Alpine Linux (Full userspace)",
        L"Boot Alpine with a serial login prompt and working apk",
        (CHAR8 *)"console=tty0 console=ttyS0 rdinit=/sbin/init video=efifb"
    },
    {
        L"Alpine Linux (Debug)",
        L"Boot Alpine with kernel debug logging enabled",
        (CHAR8 *)"console=tty0 console=ttyS0 rdinit=/sbin/init debug video=efifb"
    },
    {
        L"Reboot System",
        L"Reset system via UEFI Runtime Services",
        NULL
    }
};

static void print_countdown_tick(UINTN remaining)
{
    ST->ConOut->SetCursorPosition(ST->ConOut, 0, 14);
    console_set_color(COLOR_MUTED);
    if (remaining > 0) {
        Print(L"Automatic boot in %d seconds (Press any key to pause)...   ", remaining);
    } else {
        Print(L"Booting default entry...                                       \n");
    }
    console_set_color(COLOR_NORMAL);
}

static void draw_menu(INTN selected_index)
{
    console_clear();

    console_set_color(COLOR_HIGHLIGHT);
    Print(L"============================================================\n");
    Print(L"                  CS330 UEFI BOOTLOADER                     \n");
    Print(L"============================================================\n\n");
    console_set_color(COLOR_NORMAL);

    Print(L"Use UP/DOWN arrows to navigate, ENTER to select:\n\n");

    for (INTN i = 0; i < MAX_MENU_ENTRIES; i++) {
        if (i == selected_index) {
            console_set_color(COLOR_SELECTED);
            Print(L"  -> [ %s ] \n", menu_entries[i].title);
            console_set_color(COLOR_NORMAL);
        } else {
            console_set_color(COLOR_NORMAL);
            Print(L"      %s   \n", menu_entries[i].title);
        }
    }

    ST->ConOut->SetCursorPosition(ST->ConOut, 0, 11);
    console_set_color(COLOR_MUTED);
    Print(L"Description: %s\n", menu_entries[selected_index].description);
    console_set_color(COLOR_NORMAL);
}

static INTN run_boot_menu(void)
{
    INTN selected = 0;
    EFI_INPUT_KEY key;

    draw_menu(selected);

    BOOLEAN key_pressed = console_wait_key_timeout(5, &key, print_countdown_tick);
    if (!key_pressed) {
        return 0;
    }

    ST->ConOut->SetCursorPosition(ST->ConOut, 0, 14);
    Print(L"                                                                 \n");

    while (TRUE) {
        if (key.ScanCode == SCAN_UP) {
            if (selected > 0) selected--;
            else selected = MAX_MENU_ENTRIES - 1;
            draw_menu(selected);
        } else if (key.ScanCode == SCAN_DOWN) {
            if (selected < MAX_MENU_ENTRIES - 1) selected++;
            else selected = 0;
            draw_menu(selected);
        } else if (key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
            return selected;
        }

        UINTN index;
        BS->WaitForEvent(1, &ST->ConIn->WaitForKey, &index);
        key = console_read_key();
    }
}

EFI_STATUS EFIAPI
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    InitializeLib(image_handle, system_table);
    console_init();

    INTN choice = run_boot_menu();

    console_clear();
    console_set_color(COLOR_HIGHLIGHT);
    Print(L"\n[Bootloader] Selected: %s\n\n", menu_entries[choice].title);
    console_set_color(COLOR_NORMAL);

    if (choice == 2) {
        Print(L"Resetting system via RT->ResetSystem...\n");
        BS->Stall(1000000);
        RT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
        return EFI_SUCCESS;
    }

    /* 1. Mount ESP filesystem */
    Print(L"Initializing UEFI FileSystem...\n");
    EFI_STATUS status = file_init(image_handle);
    if (EFI_ERROR(status)) {
        console_set_color(EFI_LIGHTRED | EFI_BACKGROUND_BLACK);
        Print(L"Error: Failed to open ESP filesystem: %r\n", status);
        console_set_color(COLOR_NORMAL);
        return status;
    }

    /* 2. Load Alpine Linux kernel from ESP */
    VOID *kernel_buf = NULL;
    UINTN kernel_size = 0;
    Print(L"Loading \\vmlinuz-virt from ESP...\n");
    status = file_read(L"vmlinuz-virt", &kernel_buf, &kernel_size);
    if (EFI_ERROR(status)) {
        console_set_color(EFI_LIGHTRED | EFI_BACKGROUND_BLACK);
        Print(L"Error loading kernel: %r\n", status);
        console_set_color(COLOR_NORMAL);
        file_cleanup();
        return status;
    }
    Print(L"Loaded kernel: %d bytes (0x%lx)\n", kernel_size, (UINT64)kernel_buf);

    /* 3. Load the full Alpine root filesystem (cpio.gz initramfs) from ESP */
    VOID *initrd_buf = NULL;
    UINTN initrd_size = 0;
    Print(L"Loading \\alpine-rootfs.cpio.gz from ESP...\n");
    status = file_read(L"alpine-rootfs.cpio.gz", &initrd_buf, &initrd_size);
    if (EFI_ERROR(status)) {
        console_set_color(EFI_LIGHTRED | EFI_BACKGROUND_BLACK);
        Print(L"Error loading rootfs: %r (run 'make rootfs')\n", status);
        console_set_color(COLOR_NORMAL);
        BS->FreePool(kernel_buf);
        file_cleanup();
        return status;
    }
    Print(L"Loaded rootfs: %d bytes (0x%lx)\n\n", initrd_size, (UINT64)initrd_buf);

    console_set_color(COLOR_HIGHLIGHT);
    Print(L"[Phase 3] Booting Linux kernel now...\n");
    console_set_color(COLOR_NORMAL);

    /* 4. Hand off to Linux */
    status = linux_boot(
        image_handle,
        kernel_buf,
        kernel_size,
        initrd_buf,
        initrd_size,
        menu_entries[choice].cmdline
    );

    console_set_color(EFI_LIGHTRED | EFI_BACKGROUND_BLACK);
    Print(L"\nFatal: linux_boot failed with status: %r\n", status);
    console_set_color(COLOR_NORMAL);

    BS->FreePool(kernel_buf);
    BS->FreePool(initrd_buf);
    file_cleanup();

    return status;
}
