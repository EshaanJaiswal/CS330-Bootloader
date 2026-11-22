#include <efi.h>
#include <efilib.h>
#include "console.h"

#define MAX_MENU_ENTRIES 3

typedef struct {
    CHAR16 *title;
    CHAR16 *description;
} BootEntry;

static BootEntry menu_entries[MAX_MENU_ENTRIES] = {
    { L"Alpine Linux (Default)",        L"Boot Linux kernel with standard options" },
    { L"Alpine Linux (Recovery Console)", L"Boot into single-user maintenance mode" },
    { L"Reboot System",                  L"Reset system via UEFI Runtime Services" }
};

static void print_countdown_tick(UINTN remaining)
{
    /* Set cursor to bottom and print timer */
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

    /* Print description of currently selected entry */
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

    /* Stage 1: Countdown timer. If countdown expires, choose default (index 0). */
    BOOLEAN key_pressed = console_wait_key_timeout(5, &key, print_countdown_tick);

    if (!key_pressed) {
        return 0; /* Default boot */
    }

    /* Clear countdown line once key is pressed */
    ST->ConOut->SetCursorPosition(ST->ConOut, 0, 14);
    Print(L"                                                                 \n");

    /* Stage 2: Interactive loop */
    while (TRUE) {
        /* If we already caught a key from interrupting the countdown, handle it first */
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

        /* Wait for next keystroke */
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
    Print(L"\n[Bootloader] Selected option %d: %s\n\n", choice + 1, menu_entries[choice].title);
    console_set_color(COLOR_NORMAL);

    if (choice == 2) {
        Print(L"Resetting system via RT->ResetSystem...\n");
        BS->Stall(1000000); /* 1 second */
        RT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    } else {
        Print(L"Ready for Phase 2: Loading kernel & initramfs...\n");
        Print(L"(Press any key to exit back to firmware)\n");
        EFI_INPUT_KEY k;
        UINTN idx;
        BS->WaitForEvent(1, &ST->ConIn->WaitForKey, &idx);
        k = console_read_key();
        (void)k;
    }

    return EFI_SUCCESS;
}
