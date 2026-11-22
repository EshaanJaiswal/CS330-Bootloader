#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    InitializeLib(image_handle, system_table);

    Print(L"Hello from my bootloader!\n");
    Print(L"Firmware vendor: %s\n", system_table->FirmwareVendor);

    return EFI_SUCCESS;
}
