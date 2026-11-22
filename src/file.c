#include "file.h"

static EFI_FILE_HANDLE root_dir = NULL;

EFI_STATUS file_init(EFI_HANDLE image_handle)
{
    EFI_STATUS status;
    UINTN num_handles = 0;
    EFI_HANDLE *handles = NULL;

    (void)image_handle;

    /*
     * Enumerate all filesystem handles on the system supporting SimpleFileSystemProtocol.
     * This avoids device-handle ambiguity on virtual block devices (like QEMU's vvfat driver).
     */
    status = BS->LocateHandleBuffer(
        ByProtocol,
        &FileSystemProtocol,
        NULL,
        &num_handles,
        &handles
    );

    if (EFI_ERROR(status) || num_handles == 0) {
        Print(L"  [file_init] No filesystem handles found: %r\n", status);
        return EFI_NOT_FOUND;
    }

    Print(L"  [file_init] Found %d filesystem handle(s).\n", num_handles);

    /* Try to open root on each handle until we find our files */
    for (UINTN i = 0; i < num_handles; i++) {
        EFI_FILE_HANDLE root = LibOpenRoot(handles[i]);
        if (root) {
            EFI_FILE_HANDLE test_handle = NULL;
            /* Check if this volume has our kernel */
            status = root->Open(root, &test_handle, L"vmlinuz-virt", EFI_FILE_MODE_READ, 0);
            if (!EFI_ERROR(status)) {
                test_handle->Close(test_handle);
                root_dir = root;
                Print(L"  [file_init] ESP volume identified on handle %p!\n", handles[i]);
                break;
            }
            root->Close(root);
        }
    }

    FreePool(handles);

    if (!root_dir) {
        Print(L"  [file_init] Could not find ESP volume containing vmlinuz-virt.\n");
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

    /* 1. Open the file in read mode */
    status = root_dir->Open(root_dir, &file_handle, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* 2. Query file size via gnu-efi's LibFileInfo helper */
    file_info = LibFileInfo(file_handle);
    if (!file_info) {
        file_handle->Close(file_handle);
        return EFI_DEVICE_ERROR;
    }

    UINTN file_size = file_info->FileSize;
    FreePool(file_info);

    /* 3. Allocate memory buffer for file contents */
    status = BS->AllocatePool(EfiBootServicesData, file_size, buffer);
    if (EFI_ERROR(status)) {
        file_handle->Close(file_handle);
        return status;
    }

    /* 4. Read file into buffer */
    UINTN read_size = file_size;
    status = file_handle->Read(file_handle, &read_size, *buffer);
    file_handle->Close(file_handle);

    if (EFI_ERROR(status)) {
        BS->FreePool(*buffer);
        *buffer = NULL;
        return status;
    }

    *size = read_size;
    return EFI_SUCCESS;
}

void file_cleanup(void)
{
    if (root_dir) {
        root_dir->Close(root_dir);
        root_dir = NULL;
    }
}
