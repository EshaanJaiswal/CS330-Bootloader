#ifndef FILE_H
#define FILE_H

#include <efi.h>
#include <efilib.h>

/* Initialize filesystem protocol on the volume hosting our bootloader.
 * Returns EFI_SUCCESS if root directory of ESP was opened. */
EFI_STATUS file_init(EFI_HANDLE image_handle);

/* Read entire file from ESP root into a newly allocated buffer (AllocatePool).
 * Caller is responsible for FreePool(*buffer) when done.
 * *size receives the total bytes read. */
EFI_STATUS file_read(CHAR16 *path, VOID **buffer, UINTN *size);

/* Close root directory */
void file_cleanup(void);

#endif /* FILE_H */

