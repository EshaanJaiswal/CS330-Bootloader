#include <efi.h>
#include <efilib.h>
#include "console.h"
#include "file.h"

#define MAX_MENU_ENTRIES 3

typedef struct {
    CHAR16 *title;
    CHAR16 *description;
} BootEntry;

static BootEntry menu_entries[MAX_MENU_ENTRIES] = {
    { L"Alpine Linux (Default)",          L"Boot Linux kernel with standard options" },
    { L"Alpine Linux (Recovery Console)",   L"Boot into single-user maintenance mode" },
    { L"Reboot System",                    L"Reset system via UEFI Runtime Services" }
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

    /* Phase 2: Test File System and Load Alpine binaries */
    Print(L"Initializing UEFI SimpleFileSystemProtocol...\n");
    EFI_STATUS status = file_init(image_handle);
    if (EFI_ERROR(status)) {
        console_set_color(EFI_LIGHTRED | EFI_BACKGROUND_BLACK);
        Print(L"Error: Failed to open ESP filesystem: %r\n", status);
        console_set_color(COLOR_NORMAL);
        return status;
    }
    Print(L"Filesystem opened successfully.\n\n");

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
    Print(L"Loaded kernel: %d bytes at address 0x%lx\n", kernel_size, (UINT64)kernel_buf);

    /* Verify Linux boot header signature ('HdrS' = 0x53726448 at offset 0x202) */
    UINT8 *hdr = (UINT8 *)kernel_buf;
    if (kernel_size > 0x206 &&
        hdr[0x202] == 'H' && hdr[0x203] == 'd' &&
        hdr[0x204] == 'r' && hdr[0x205] == 'S') {
        UINT16 boot_proto = *(UINT16 *)(hdr + 0x206);
        console_set_color(COLOR_HIGHLIGHT);
        Print(L"[OK] Valid Linux kernel detected! Boot protocol: v%x.%02x\n\n",
              (boot_proto >> 8), (boot_proto & 0xFF));
        console_set_color(COLOR_NORMAL);
    }

    VOID *initrd_buf = NULL;
    UINTN initrd_size = 0;
    Print(L"Loading \\initramfs-virt from ESP...\n");
    status = file_read(L"initramfs-virt", &initrd_buf, &initrd_size);
    if (EFI_ERROR(status)) {
        console_set_color(EFI_LIGHTRED | EFI_BACKGROUND_BLACK);
        Print(L"Error loading initramfs: %r\n", status);
        console_set_color(COLOR_NORMAL);
        BS->FreePool(kernel_buf);
        file_cleanup();
        return status;
    }
    Print(L"Loaded initramfs: %d bytes at address 0x%lx\n\n", initrd_size, (UINT64)initrd_buf);

    console_set_color(COLOR_HIGHLIGHT);
    Print(L"Phase 2 Complete: Kernel and initramfs are in memory!\n");
    console_set_color(COLOR_NORMAL);
    Print(L"Ready for Phase 3: Setup boot_params & ExitBootServices handoff.\n");
    Print(L"(Press any key to exit)\n");

    EFI_INPUT_KEY k;
    UINTN idx;
    BS->WaitForEvent(1, &ST->ConIn->WaitForKey, &idx);
    k = console_read_key();
    (void)k;

    BS->FreePool(kernel_buf);
    BS->FreePool(initrd_buf);
    file_cleanup();

    return EFI_SUCCESS;
}
