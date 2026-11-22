#include "file.h"

static EFI_FILE_HANDLE root_dir = NULL;

EFI_STATUS file_init(EFI_HANDLE image_handle)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE *loaded_image = NULL;

    /* Print debug step */
    Print(L"  [debug] querying LoadedImage protocol on image_handle %p...\n", image_handle);

    /* OpenProtocol with EFI_OPEN_PROTOCOL_GET_PROTOCOL is the modern UEFI standard */
    status = BS->OpenProtocol(
        image_handle,
        &LoadedImageProtocol,
        (VOID **)&loaded_image,
        image_handle,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );

    if (EFI_ERROR(status)) {
        Print(L"  [debug] OpenProtocol failed: %r (%lx)\n", status, status);
        /* Fallback: try HandleProtocol directly */
        status = BS->HandleProtocol(image_handle, &LoadedImageProtocol, (VOID **)&loaded_image);
        if (EFI_ERROR(status)) {
            Print(L"  [debug] HandleProtocol also failed: %r (%lx)\n", status, status);
            return status;
        }
    }

    Print(L"  [debug] LoadedImage ok. DeviceHandle: %p\n", loaded_image->DeviceHandle);

    /* Open root filesystem using LibOpenRoot */
    root_dir = LibOpenRoot(loaded_image->DeviceHandle);
    if (!root_dir) {
        Print(L"  [debug] LibOpenRoot returned NULL for DeviceHandle %p\n", loaded_image->DeviceHandle);
        return EFI_NOT_FOUND;
    }

    Print(L"  [debug] LibOpenRoot succeeded! root_dir: %p\n", root_dir);
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
