#include "file.h"

static EFI_FILE_HANDLE root_dir = NULL;

EFI_STATUS file_init(EFI_HANDLE image_handle)
{
    EFI_STATUS status;
    UINTN num_handles = 0;
    EFI_HANDLE *handles = NULL;

    (void)image_handle;

    /*
     * Enumerate every handle that exposes SimpleFileSystemProtocol
     * (called FileSystemProtocol in gnu-efi = gEfiSimpleFileSystemProtocolGuid).
     * On QEMU vvfat, the loaded-image device handle does NOT support
     * HandleProtocol(SimpleFileSystemProtocol) directly, so we scan all.
     */
    status = BS->LocateHandleBuffer(
        ByProtocol,
        &FileSystemProtocol,
        NULL,
        &num_handles,
        &handles
    );

    if (EFI_ERROR(status)) {
        Print(L"  [file_init] LocateHandleBuffer failed: %r\n", status);
        return status;
    }
    if (num_handles == 0) {
        Print(L"  [file_init] No filesystem handles found.\n");
        return EFI_NOT_FOUND;
    }

    Print(L"  [file_init] Found %d filesystem handle(s).\n", num_handles);

    for (UINTN i = 0; i < num_handles; i++) {
        /* LibOpenRoot does HandleProtocol + OpenVolume internally */
        EFI_FILE_HANDLE root = LibOpenRoot(handles[i]);
        if (!root) {
            continue;
        }

        /* Probe for vmlinuz-virt to identify the ESP */
        EFI_FILE_HANDLE test_handle = NULL;
        status = root->Open(root, &test_handle, L"vmlinuz-virt",
                            EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status) && test_handle) {
            test_handle->Close(test_handle);
            root_dir = root;
            Print(L"  [file_init] ESP volume identified on handle %d!\n", i);
            break;
        }
        root->Close(root);
    }

    BS->FreePool(handles);

    if (!root_dir) {
        Print(L"  [file_init] vmlinuz-virt not found on any filesystem.\n");
        return EFI_NOT_FOUND;
    }

    return EFI_SUCCESS;
}

EFI_STATUS file_read(CHAR16 *path, VOID **buffer, UINTN *size)
{
    EFI_STATUS status;
    EFI_FILE_HANDLE file_handle = NULL;
    EFI_FILE_INFO *file_info = NULL;

    if (!root_dir || !path || !buffer || !size) {
        return EFI_INVALID_PARAMETER;
    }

    /* 1. Open file */
    status = root_dir->Open(root_dir, &file_handle, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* 2. Query file size */
    file_info = LibFileInfo(file_handle);
    if (!file_info) {
        file_handle->Close(file_handle);
        return EFI_DEVICE_ERROR;
    }

    UINTN file_size = file_info->FileSize;
    FreePool(file_info);

    /* 3. Allocate buffer */
    status = BS->AllocatePool(EfiBootServicesData, file_size, buffer);
    if (EFI_ERROR(status)) {
        file_handle->Close(file_handle);
        return status;
    }

    /* 4. Read file in a loop until all bytes are transferred */
    UINT8 *dest = (UINT8 *)*buffer;
    UINTN total_read = 0;

    while (total_read < file_size) {
        UINTN chunk = file_size - total_read;
        status = file_handle->Read(file_handle, &chunk, dest + total_read);
        if (EFI_ERROR(status) || chunk == 0) {
            break;
        }
        total_read += chunk;
    }

    file_handle->Close(file_handle);

    if (total_read != file_size) {
        Print(L"  [file_read] Short read: got %d of %d bytes\n",
              total_read, file_size);
        BS->FreePool(*buffer);
        *buffer = NULL;
        return EFI_DEVICE_ERROR;
    }

    *size = total_read;
    return EFI_SUCCESS;
}

void file_cleanup(void)
{
    if (root_dir) {
        root_dir->Close(root_dir);
        root_dir = NULL;
    }
}
