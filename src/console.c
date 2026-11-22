#include "console.h"

void console_init(void)
{
    if (ST && ST->ConOut) {
        ST->ConOut->EnableCursor(ST->ConOut, FALSE);
        console_clear();
        console_set_color(COLOR_NORMAL);
    }
}

void console_clear(void)
{
    if (ST && ST->ConOut) {
        ST->ConOut->ClearScreen(ST->ConOut);
    }
}

void console_set_color(UINTN attribute)
{
    if (ST && ST->ConOut) {
        ST->ConOut->SetAttribute(ST->ConOut, attribute);
    }
}

BOOLEAN console_has_key(void)
{
    if (!ST || !ST->ConIn)
        return FALSE;

    /* CheckWaitKey event is signaled if a key is ready in buffer */
    EFI_STATUS status = BS->CheckEvent(ST->ConIn->WaitForKey);
    return (status == EFI_SUCCESS);
}

EFI_INPUT_KEY console_read_key(void)
{
    EFI_INPUT_KEY key;
    ZeroMem(&key, sizeof(EFI_INPUT_KEY));

    if (ST && ST->ConIn) {
        ST->ConIn->ReadKeyStroke(ST->ConIn, &key);
    }
    return key;
}

BOOLEAN console_wait_key_timeout(UINTN seconds, EFI_INPUT_KEY *out_key, void (*on_tick)(UINTN remaining))
{
    UINTN remaining = seconds;

    while (remaining > 0) {
        if (on_tick) {
            on_tick(remaining);
        }

        /* 1 second = 10 slices of 100ms so input is responsive */
        for (int slice = 0; slice < 10; slice++) {
            if (console_has_key()) {
                if (out_key) {
                    *out_key = console_read_key();
                }
                return TRUE;
            }
            /* Stall for 100,000 microseconds = 100ms */
            BS->Stall(100000);
        }

        remaining--;
    }

    if (on_tick) {
        on_tick(0);
    }
    return FALSE;
}

