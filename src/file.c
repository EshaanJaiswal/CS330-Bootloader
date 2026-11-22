#include "file.h"

static EFI_FILE_HANDLE root_dir = NULL;

EFI_STATUS file_init(EFI_HANDLE image_handle)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE *loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_GUID loaded_image_guid = LOADED_IMAGE_PROTOCOL;
    EFI_GUID fs_guid = SIMPLE_FILE_SYSTEM_PROTOCOL;

    /* 1. Retrieve the LoadedImage protocol for our running image */
    status = BS->HandleProtocol(image_handle, &loaded_image_guid, (VOID **)&loaded_image);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* 2. Retrieve SimpleFileSystem from the device handle where we were loaded */
    status = BS->HandleProtocol(loaded_image->DeviceHandle, &fs_guid, (VOID **)&fs);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* 3. Open the root directory of this filesystem */
    status = fs->OpenVolume(fs, &root_dir);
    return status;
}

EFI_STATUS file_read(CHAR16 *path, VOID **buffer, UINTN *size)
{
    EFI_STATUS status;
    EFI_FILE_HANDLE file_handle = NULL;
    EFI_FILE_INFO *file_info = NULL;
    UINTN info_size = 0;
    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;

    if (!root_dir || !path || !buffer || !size) {
        return EFI_INVALID_PARAMETER;
    }

    /* 1. Open the file in read mode */
    status = root_dir->Open(root_dir, &file_handle, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* 2. Query file size via GetInfo (first call returns required buffer size) */
    status = file_handle->GetInfo(file_handle, &file_info_guid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL) {
        file_handle->Close(file_handle);
        return status;
    }

    status = BS->AllocatePool(EfiBootServicesData, info_size, (VOID **)&file_info);
    if (EFI_ERROR(status)) {
        file_handle->Close(file_handle);
        return status;
    }

    status = file_handle->GetInfo(file_handle, &file_info_guid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        BS->FreePool(file_info);
        file_handle->Close(file_handle);
        return status;
    }

    UINTN file_size = file_info->FileSize;
    BS->FreePool(file_info);

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
