#include "linux_boot.h"

static UINT32 uefi_to_e820_type(UINT32 uefi_type)
{
    switch (uefi_type) {
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiConventionalMemory:
            return E820_RAM;

        case EfiACPIReclaimMemory:
            return E820_ACPI;

        case EfiACPIMemoryNVS:
            return E820_NVS;

        case EfiReservedMemoryType:
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace:
        case EfiPalCode:
        default:
            return E820_RESERVED;
    }
}

static UINT8 build_e820_map(
    struct boot_e820_entry *e820_table,
    EFI_MEMORY_DESCRIPTOR *mmap,
    UINTN mmap_size,
    UINTN desc_size
) {
    UINT8 count = 0;
    UINT8 *ptr = (UINT8 *)mmap;
    UINTN num_desc = mmap_size / desc_size;

    for (UINTN i = 0; i < num_desc && count < E820_MAX_ENTRIES; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(ptr + (i * desc_size));
        UINT32 e820_type = uefi_to_e820_type(desc->Type);

        if (count > 0 &&
            e820_table[count - 1].type == e820_type &&
            (e820_table[count - 1].addr + e820_table[count - 1].size) == desc->PhysicalStart) {
            e820_table[count - 1].size += (desc->NumberOfPages * EFI_PAGE_SIZE);
        } else {
            e820_table[count].addr = desc->PhysicalStart;
            e820_table[count].size = desc->NumberOfPages * EFI_PAGE_SIZE;
            e820_table[count].type = e820_type;
            count++;
        }
    }

    return count;
}

EFI_STATUS linux_boot(
    EFI_HANDLE image_handle,
    VOID *kernel_buf,
    UINTN kernel_size,
    VOID *initrd_buf,
    UINTN initrd_size,
    CHAR8 *cmdline
) {
    EFI_STATUS status;

    if (!kernel_buf || kernel_size < 0x290) {
        return EFI_INVALID_PARAMETER;
    }

    /* 1. Allocate boot_params anywhere below 1GB (avoid hardcoding 0x90000 EBDA conflict) */
    UINTN bp_pages = (sizeof(struct boot_params) + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS bp_addr = 0x3fffffff;
    status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, bp_pages, &bp_addr);
    if (EFI_ERROR(status)) {
        status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, bp_pages, &bp_addr);
        if (EFI_ERROR(status)) {
            Print(L"Failed to allocate memory for boot_params: %r\n", status);
            return status;
        }
    }

    struct boot_params *bp = (struct boot_params *)(UINTN)bp_addr;
    ZeroMem(bp, bp_pages * EFI_PAGE_SIZE);
    Print(L"boot_params allocated at 0x%lx\n", bp_addr);

    /* 2. Read setup_header from kernel image */
    UINT8 *kbytes = (UINT8 *)kernel_buf;
    struct setup_header *orig_hdr = (struct setup_header *)(kbytes + 0x1f1);

    CopyMem(&bp->hdr, orig_hdr, sizeof(struct setup_header));

    UINT8 setup_sects = orig_hdr->setup_sects;
    if (setup_sects == 0) {
        setup_sects = 4;
    }
    UINTN setup_size = (setup_sects + 1) * 512;
    UINTN payload_size = kernel_size - setup_size;

    Print(L"Linux kernel setup size: 0x%lx, payload size: 0x%lx\n", setup_size, payload_size);

    /* 3. Allocate pages for the 64-bit protected-mode kernel payload */
    UINT64 pref_addr = orig_hdr->pref_address;
    if (pref_addr == 0) {
        pref_addr = 0x1000000;
    }
    UINT32 init_size = orig_hdr->init_size;
    if (init_size < payload_size) {
        init_size = payload_size;
    }

    UINTN kernel_pages = (init_size + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS kernel_dest = pref_addr;

    status = BS->AllocatePages(AllocateAddress, EfiLoaderData, kernel_pages, &kernel_dest);
    if (EFI_ERROR(status)) {
        Print(L"Preferred address 0x%lx busy, allocating anywhere below 4GB...\n", pref_addr);
        kernel_dest = 0x3fffffff;
        status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, kernel_pages, &kernel_dest);
        if (EFI_ERROR(status)) {
            Print(L"Failed to allocate memory for kernel payload: %r\n", status);
            return status;
        }
    }

    ZeroMem((VOID *)(UINTN)kernel_dest, kernel_pages * EFI_PAGE_SIZE);
    CopyMem((VOID *)(UINTN)kernel_dest, kbytes + setup_size, payload_size);
    Print(L"Kernel payload copied to 0x%lx (%d pages)\n", kernel_dest, kernel_pages);

    /* 4. Set up Command Line */
    if (cmdline) {
        UINTN cmdlen = 0;
        while (cmdline[cmdlen]) cmdlen++;
        cmdlen++;

        EFI_PHYSICAL_ADDRESS cmd_dest = 0x3fffffff;
        UINTN cmd_pages = (cmdlen + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, cmd_pages, &cmd_dest);
        if (EFI_ERROR(status)) {
            Print(L"Failed to allocate memory for cmdline: %r\n", status);
            return status;
        }
        CopyMem((VOID *)(UINTN)cmd_dest, cmdline, cmdlen);
        bp->hdr.cmd_line_ptr = (UINT32)cmd_dest;
        bp->ext_cmd_line_ptr = 0;
        Print(L"Command line placed at 0x%lx: \"%a\"\n", cmd_dest, cmdline);
    }

    /* 5. Set up Initramfs */
    if (initrd_buf && initrd_size > 0) {
        UINTN initrd_pages = (initrd_size + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        EFI_PHYSICAL_ADDRESS initrd_dest = 0x1fffffff; /* Under 512MB */

        status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, initrd_pages, &initrd_dest);
        if (EFI_ERROR(status)) {
            initrd_dest = 0x3fffffff;
            status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, initrd_pages, &initrd_dest);
            if (EFI_ERROR(status)) {
                Print(L"Failed to allocate memory for initrd: %r\n", status);
                return status;
            }
        }

        ZeroMem((VOID *)(UINTN)initrd_dest, initrd_pages * EFI_PAGE_SIZE);
        CopyMem((VOID *)(UINTN)initrd_dest, initrd_buf, initrd_size);

        bp->hdr.ramdisk_image = (UINT32)initrd_dest;
        bp->hdr.ramdisk_size = (UINT32)initrd_size;
        bp->ext_ramdisk_image = 0;
        bp->ext_ramdisk_size = 0;

        UINT8 *dest_bytes = (UINT8 *)(UINTN)initrd_dest;
        Print(L"Initramfs placed at 0x%lx (%d bytes, magic: %02X %02X %02X %02X)\n",
              initrd_dest, initrd_size,
              dest_bytes[0], dest_bytes[1], dest_bytes[2], dest_bytes[3]);
    }

    /* 6. Populate required loader fields */
    bp->hdr.type_of_loader = 0xFF;
    bp->hdr.loadflags |= 0x01;
    bp->hdr.code32_start = (UINT32)kernel_dest;

    /* 7. Retrieve UEFI Memory Map and call ExitBootServices */
    Print(L"Preparing to ExitBootServices and hand over control...\n");
    BS->Stall(1000000);

    UINTN mmap_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;

    status = BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
    mmap_size += (4 * desc_size);

    status = BS->AllocatePool(EfiLoaderData, mmap_size, (VOID **)&mmap);
    if (EFI_ERROR(status)) {
        Print(L"Failed to allocate memory map buffer: %r\n", status);
        return status;
    }

    status = BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        Print(L"Failed to get memory map: %r\n", status);
        return status;
    }

    bp->e820_entries = build_e820_map(bp->e820_table, mmap, mmap_size, desc_size);

    status = BS->ExitBootServices(image_handle, map_key);
    if (EFI_ERROR(status)) {
        mmap_size += (4 * desc_size);
        BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
        bp->e820_entries = build_e820_map(bp->e820_table, mmap, mmap_size, desc_size);
        status = BS->ExitBootServices(image_handle, map_key);
        if (EFI_ERROR(status)) {
            return status;
        }
    }

    /* 8. Jump to 64-bit kernel entry point (startup_64) */
    UINT64 kernel_entry = kernel_dest + 0x200;

    __asm__ __volatile__(
        "cli\n\t"
        "xor %%rax, %%rax\n\t"
        "mov %%rax, %%ds\n\t"
        "mov %%rax, %%es\n\t"
        "mov %%rax, %%fs\n\t"
        "mov %%rax, %%gs\n\t"
        "mov %%rax, %%ss\n\t"
        "xor %%rax, %%rax\n\t"
        "xor %%rbx, %%rbx\n\t"
        "xor %%rbp, %%rbp\n\t"
        "xor %%rdx, %%rdx\n\t"
        "jmp *%%rcx\n\t"
        :
        : "c"(kernel_entry), "S"((UINT64)bp)
        : "memory"
    );

    while (1) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}
