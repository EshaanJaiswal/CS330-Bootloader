#ifndef CONSOLE_H
#define CONSOLE_H

#include <efi.h>
#include <efilib.h>

/* Colors available in UEFI SimpleTextOutput */
#define COLOR_NORMAL    (EFI_LIGHTGRAY | EFI_BACKGROUND_BLACK)
#define COLOR_HIGHLIGHT (EFI_YELLOW    | EFI_BACKGROUND_BLACK)
#define COLOR_MUTED     (EFI_DARKGRAY  | EFI_BACKGROUND_BLACK)
#define COLOR_SELECTED  (EFI_BLACK     | EFI_BACKGROUND_LIGHTGRAY)

/* Basic console operations */
void console_init(void);
void console_clear(void);
void console_set_color(UINTN attribute);

/* Non-blocking key check (returns 0 if no key pressed, or scan code / unicode) */
BOOLEAN console_has_key(void);
EFI_INPUT_KEY console_read_key(void);

/* Wait for key with a timeout in seconds.
 * Returns TRUE if a key was pressed within timeout (key stored in *out_key),
 * or FALSE if timeout expired. If on_tick is non-NULL, called each second. */
BOOLEAN console_wait_key_timeout(UINTN seconds, EFI_INPUT_KEY *out_key, void (*on_tick)(UINTN remaining));

#endif /* CONSOLE_H */
