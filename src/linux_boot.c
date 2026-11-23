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

/*
 * Forward the ACPI RSDP pointer from UEFI's configuration table into
 * boot_params (offset 0x70).  Without this the kernel reports
 * "A valid RSDP was not found" and disables ACPI under this boot protocol.
 * Note that acpi_rsdp_addr must hold the pointer VALUE, not the bytes stored
 * at that address.
 */
static void fill_acpi_rsdp(struct boot_params *bp)
{
    static EFI_GUID acpi20_guid = ACPI_20_TABLE_GUID;
    static EFI_GUID acpi10_guid = ACPI_TABLE_GUID;
    EFI_GUID *guids[2] = { &acpi20_guid, &acpi10_guid };

    for (UINTN g = 0; g < 2; g++) {
        for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
            EFI_CONFIGURATION_TABLE *entry = &ST->ConfigurationTable[i];
            if (CompareGuid(&entry->VendorGuid, guids[g])) {
                UINT64 rsdp_addr = (UINT64)(UINTN)entry->VendorTable;
                BS->CopyMem(bp->acpi_rsdp_addr, &rsdp_addr, 8);
                return;
            }
        }
    }
}

/* Fill boot_params.screen_info from UEFI's Graphics Output Protocol.
 * GRUB does this; without it the kernel sees no framebuffer and tty0 is only
 * a dummy console, so nothing appears in QEMU's graphics window. */
static void put16(UINT8 *p, UINT16 v) { p[0] = (UINT8)v; p[1] = (UINT8)(v >> 8); }
static void put32(UINT8 *p, UINT32 v)
{
    p[0] = (UINT8)v; p[1] = (UINT8)(v >> 8);
    p[2] = (UINT8)(v >> 16); p[3] = (UINT8)(v >> 24);
}

static void fill_screen_info(struct boot_params *bp)
{
    static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    UINT8 *si = bp->screen_info;

    if (EFI_ERROR(BS->LocateProtocol(&gop_guid, NULL, (VOID **)&gop)) ||
        !gop || !gop->Mode || !gop->Mode->Info) {
        return;
    }
    info = gop->Mode->Info;

    /* VIDEO_TYPE_EFI makes the kernel use EFI framebuffer output instead of
     * binding vgacon to the nonexistent legacy VGA text buffer. */
    si[0x00] = 0;                                     /* orig_x */
    si[0x01] = 0;                                     /* orig_y */
    si[0x06] = 0;                                     /* orig_video_mode */
    si[0x07] = 0;                                     /* orig_video_cols */
    si[0x0e] = 0;                                     /* orig_video_lines */
    si[0x0f] = 0x70;                                  /* VIDEO_TYPE_EFI */

    put16(si + 0x12, (UINT16)info->HorizontalResolution);
    put16(si + 0x14, (UINT16)info->VerticalResolution);
    put16(si + 0x16, 32);                             /* lfb_depth */
    put32(si + 0x18, (UINT32)gop->Mode->FrameBufferBase);
    put32(si + 0x1c, (UINT32)gop->Mode->FrameBufferSize);
    put16(si + 0x24, (UINT16)(info->PixelsPerScanLine * 4));

    if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        si[0x26] = 8; si[0x27] = 16;   /* red */
        si[0x28] = 8; si[0x29] = 8;    /* green */
        si[0x2a] = 8; si[0x2b] = 0;    /* blue */
        si[0x2c] = 8; si[0x2d] = 24;   /* reserved */
    }

    Print(L"UEFI framebuffer: %dx%d @ 0x%lx\n",
          info->HorizontalResolution, info->VerticalResolution,
          (UINT64)gop->Mode->FrameBufferBase);
}

static void fill_efi_info(struct boot_params *bp,
                          EFI_MEMORY_DESCRIPTOR *mmap,
                          UINTN mmap_size,
                          UINTN desc_size,
                          UINT32 desc_ver)
{
    UINT8 *efi = bp->efi_info;
    UINT64 systab = (UINT64)(UINTN)ST;
    UINT64 memmap = (UINT64)(UINTN)mmap;

    efi[0] = 'E';
    efi[1] = 'F';
    efi[2] = 'I';
    efi[3] = '6';
    put32(efi + 4, (UINT32)systab);
    put32(efi + 8, (UINT32)desc_size);
    put32(efi + 12, desc_ver);
    put32(efi + 16, (UINT32)memmap);
    put32(efi + 20, (UINT32)mmap_size);
    put32(efi + 24, (UINT32)(systab >> 32));
    put32(efi + 28, (UINT32)(memmap >> 32));
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

    /* ------------------------------------------------------------------ */
    /* 1. Read setup_header from kernel image                               */
    /* ------------------------------------------------------------------ */
    UINT8 *kbytes = (UINT8 *)kernel_buf;
    struct setup_header *orig_hdr = (struct setup_header *)(kbytes + 0x1f1);

    UINT8 setup_sects = orig_hdr->setup_sects;
    if (setup_sects == 0) {
        setup_sects = 4;
    }
    UINTN setup_size  = (setup_sects + 1) * 512;
    UINTN payload_size = kernel_size - setup_size;

    Print(L"Linux kernel setup size: 0x%lx, payload size: 0x%lx\n",
          setup_size, payload_size);

    /* ------------------------------------------------------------------ */
    /* 2. Allocate boot_params below 1 GB (must NOT be in EBDA 0x9xxxx)   */
    /* ------------------------------------------------------------------ */
    UINTN bp_pages = (sizeof(struct boot_params) + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS bp_addr = 0x3fffffff;
    status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, bp_pages, &bp_addr);
    if (EFI_ERROR(status)) {
        status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, bp_pages, &bp_addr);
        if (EFI_ERROR(status)) {
            Print(L"Failed to allocate boot_params: %r\n", status);
            return status;
        }
    }

    struct boot_params *bp = (struct boot_params *)(UINTN)bp_addr;
    BS->SetMem(bp, bp_pages * EFI_PAGE_SIZE, 0);
    Print(L"boot_params allocated at 0x%lx\n", bp_addr);

    /* Pass UEFI's framebuffer to the kernel so QEMU's window shows the OS */
    fill_screen_info(bp);

    /* Forward firmware's ACPI RSDP so the kernel can find ACPI tables */
    fill_acpi_rsdp(bp);

    /* Copy the setup_header from the kernel image */
    BS->CopyMem(&bp->hdr, orig_hdr, sizeof(struct setup_header));

    /* ------------------------------------------------------------------ */
    /* 3. Allocate and copy the kernel payload                              */
    /*    pref_address is typically 0x1000000; fall back to below 1 GB     */
    /* ------------------------------------------------------------------ */
    UINT64 pref_addr = orig_hdr->pref_address;
    if (pref_addr == 0) pref_addr = 0x1000000;

    UINT32 init_size = orig_hdr->init_size;
    if (init_size == 0 || init_size < (UINT32)payload_size) {
        init_size = (UINT32)payload_size;
    }

    UINTN kernel_pages = (init_size + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS kernel_dest = pref_addr;

    status = BS->AllocatePages(AllocateAddress, EfiLoaderData,
                               kernel_pages, &kernel_dest);
    if (EFI_ERROR(status)) {
        Print(L"Preferred address 0x%lx busy, allocating anywhere below 4GB...\n",
              pref_addr);
        kernel_dest = 0x3fffffff;
        status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                   kernel_pages, &kernel_dest);
        if (EFI_ERROR(status)) {
            Print(L"Failed to allocate kernel payload memory: %r\n", status);
            return status;
        }
    }

    BS->SetMem((VOID *)(UINTN)kernel_dest, kernel_pages * EFI_PAGE_SIZE, 0);
    BS->CopyMem((VOID *)(UINTN)kernel_dest, kbytes + setup_size, payload_size);
    Print(L"Kernel payload copied to 0x%lx (%d pages)\n", kernel_dest, kernel_pages);

    /* ------------------------------------------------------------------ */
    /* 4. Kernel command line (must be below 4 GB, prefer below 1 GB)      */
    /* ------------------------------------------------------------------ */
    if (cmdline) {
        UINTN cmdlen = 0;
        while (cmdline[cmdlen]) cmdlen++;
        cmdlen++; /* include NUL terminator */

        EFI_PHYSICAL_ADDRESS cmd_dest = 0x3fffffff;
        UINTN cmd_pages = (cmdlen + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                   cmd_pages, &cmd_dest);
        if (EFI_ERROR(status)) {
            Print(L"Failed to allocate cmdline buffer: %r\n", status);
            return status;
        }
        BS->CopyMem((VOID *)(UINTN)cmd_dest, cmdline, cmdlen);
        bp->hdr.cmd_line_ptr   = (UINT32)cmd_dest;
        bp->ext_cmd_line_ptr   = (UINT32)((UINT64)cmd_dest >> 32);
        Print(L"Command line placed at 0x%lx: \"%a\"\n", cmd_dest, cmdline);
    }

    /* ------------------------------------------------------------------ */
    /* 5. Initramfs — must be below initrd_addr_max (0x7fffffff for most)  */
    /*    Place it at a fixed low address (128 MB) to avoid memblock clash  */
    /* ------------------------------------------------------------------ */
    if (initrd_buf && initrd_size > 0) {
        UINTN initrd_pages = (initrd_size + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;

        /*
         * Try a fixed low address first (128 MB = 0x08000000).
         * This is well below the kernel's preferred 0x1000000 load point
         * and below the memblock waterline that overwrites high memory.
         */
        EFI_PHYSICAL_ADDRESS initrd_dest = 0x08000000;
        status = BS->AllocatePages(AllocateAddress, EfiLoaderData,
                                   initrd_pages, &initrd_dest);
        if (EFI_ERROR(status)) {
            /* Fall back: anywhere ≤ 256 MB */
            initrd_dest = 0x0fffffff;
            status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                       initrd_pages, &initrd_dest);
        }
        if (EFI_ERROR(status)) {
            /* Last resort: anywhere ≤ initrd_addr_max (0x7fffffff) */
            initrd_dest = 0x7fffffff;
            status = BS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                       initrd_pages, &initrd_dest);
        }
        if (EFI_ERROR(status)) {
            Print(L"Failed to allocate initrd memory: %r\n", status);
            return status;
        }

        BS->SetMem((VOID *)(UINTN)initrd_dest, initrd_pages * EFI_PAGE_SIZE, 0);
        BS->CopyMem((VOID *)(UINTN)initrd_dest, initrd_buf, initrd_size);

        bp->hdr.ramdisk_image  = (UINT32)initrd_dest;
        bp->hdr.ramdisk_size   = (UINT32)initrd_size;
        bp->ext_ramdisk_image  = (UINT32)((UINT64)initrd_dest >> 32);
        bp->ext_ramdisk_size   = (UINT32)((UINT64)initrd_size >> 32);

        UINT8 *db = (UINT8 *)(UINTN)initrd_dest;
        Print(L"Initramfs placed at 0x%lx (%d bytes, magic: %02X %02X %02X %02X)\n",
              initrd_dest, initrd_size,
              db[0], db[1], db[2], db[3]);
    }

    /* ------------------------------------------------------------------ */
    /* 6. Required loader fields in setup_header                            */
    /* ------------------------------------------------------------------ */
    bp->hdr.type_of_loader  = 0xFF;         /* custom bootloader */
    bp->hdr.loadflags      |= 0x01;         /* LOADED_HIGH */
    bp->hdr.loadflags      |= 0x80;         /* CAN_USE_HEAP */
    bp->hdr.heap_end_ptr    = 0xFE00;       /* conventional heap limit */
    bp->hdr.code32_start    = (UINT32)kernel_dest;

    /* ------------------------------------------------------------------ */
    /* 7. UEFI memory map + ExitBootServices                                */
    /* ------------------------------------------------------------------ */
    Print(L"Preparing to ExitBootServices and hand over control...\n");
    BS->Stall(500000); /* 0.5 s — let serial flush */

    UINTN mmap_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;

    /* First call: get required buffer size */
    BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
    mmap_size += 8 * desc_size; /* extra slack for the AllocatePool below */

    status = BS->AllocatePool(EfiLoaderData, mmap_size, (VOID **)&mmap);
    if (EFI_ERROR(status)) {
        Print(L"Failed to allocate memory map buffer: %r\n", status);
        return status;
    }

    /* Second call: fill the buffer */
    status = BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        Print(L"Failed to get memory map: %r\n", status);
        return status;
    }

    bp->e820_entries = build_e820_map(bp->e820_table, mmap, mmap_size, desc_size);
    fill_efi_info(bp, mmap, mmap_size, desc_size, desc_ver);

    /* ExitBootServices — retry once if map key was stale.
     * IMPORTANT: no allocations (not even Print) between GetMemoryMap and EBS. */
    status = BS->ExitBootServices(image_handle, map_key);
    if (EFI_ERROR(status)) {
        /* ExitBootServices can invalidate the map key after the first call.
         * Reacquire and rebuild the map before retrying, without using stale
         * map buffers or map keys. */
        BS->FreePool(mmap);
        mmap = NULL;

        mmap_size = 0;
        BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
        mmap_size += 8 * desc_size;

        status = BS->AllocatePool(EfiLoaderData, mmap_size, (VOID **)&mmap);
        if (EFI_ERROR(status)) {
            return status;
        }

        status = BS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
        if (EFI_ERROR(status)) {
            BS->FreePool(mmap);
            return status;
        }

        bp->e820_entries = build_e820_map(bp->e820_table, mmap, mmap_size, desc_size);
        fill_efi_info(bp, mmap, mmap_size, desc_size, desc_ver);
        status = BS->ExitBootServices(image_handle, map_key);
        if (EFI_ERROR(status)) {
            /* Cannot print — boot services are unavailable after EBS failure. */
            return status;
        }
    }

    /* ------------------------------------------------------------------ */
    /* 8. Jump to 64-bit kernel entry (startup_64) at kernel_dest + 0x200  */
    /*                                                                      */
    /* Linux boot protocol (x86-64):                                        */
    /*   %rsi = pointer to boot_params (zero page)                          */
    /*   %rdi = 0  (not used by startup_64 but zero it for safety)          */
    /*   %rsp = any valid stack pointer (we keep UEFI's stack)              */
    /*   CS   = flat 64-bit code segment (already set by OVMF)              */
    /*                                                                      */
    /* DO NOT touch segment registers — they are already set up correctly   */
    /* by OVMF. Zeroing %ss in particular causes an immediate triple fault. */
    /* ------------------------------------------------------------------ */
    UINT64 kernel_entry = kernel_dest + 0x200;

    __asm__ __volatile__(
        "cli\n\t"
        "jmp *%0\n\t"
        :
        : "r"(kernel_entry),
          "S"((UINT64)(UINTN)bp)   /* %rsi = boot_params */
        : "memory"
    );

    /* Should never reach here */
    while (1) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}
