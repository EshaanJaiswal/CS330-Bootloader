#ifndef LINUX_BOOT_H
#define LINUX_BOOT_H

#include <efi.h>
#include <efilib.h>

#define E820_RAM          1
#define E820_RESERVED     2
#define E820_ACPI         3
#define E820_NVS          4
#define E820_UNUSABLE     5

#define E820_MAX_ENTRIES  128

#pragma pack(push, 1)

struct boot_e820_entry {
    UINT64 addr;
    UINT64 size;
    UINT32 type;
};

/* Setup header as documented in Documentation/arch/x86/boot.rst */
struct setup_header {
    UINT8   setup_sects;
    UINT16  root_flags;
    UINT32  syssize;
    UINT16  ram_size;
    UINT16  vid_mode;
    UINT16  root_dev;
    UINT16  boot_flag;
    UINT16  jump;
    UINT32  header;             /* 0x202: "HdrS" */
    UINT16  version;            /* 0x206: boot protocol version */
    UINT32  realmode_swtch;
    UINT16  start_sys_seg;
    UINT16  kernel_version;
    UINT8   type_of_loader;     /* 0x210: 0xFF for custom loader */
    UINT8   loadflags;          /* 0x211: bit 0: LOADED_HIGH */
    UINT16  setup_move_size;
    UINT32  code32_start;
    UINT32  ramdisk_image;      /* 0x218: 32-bit physical address of initrd */
    UINT32  ramdisk_size;       /* 0x21c: size of initrd */
    UINT32  bootsect_kludge;
    UINT16  heap_end_ptr;
    UINT8   ext_loader_ver;
    UINT8   ext_loader_type;
    UINT32  cmd_line_ptr;       /* 0x228: 32-bit physical address of cmdline */
    UINT32  initrd_addr_max;    /* 0x22c: max physical address for initrd */
    UINT32  kernel_alignment;
    UINT8   relocatable_kernel;
    UINT8   min_alignment;
    UINT16  xloadflags;
    UINT32  cmdline_size;
    UINT32  hardware_subarch;
    UINT64  hardware_subarch_data;
    UINT32  payload_offset;
    UINT32  payload_length;
    UINT64  setup_data;
    UINT64  pref_address;       /* 0x258: 64-bit preferred load address */
    UINT32  init_size;          /* 0x260: memory required to decompress/run */
    UINT32  handover_offset;    /* 0x264: UEFI handover entry offset */
};

/* The standard Linux boot_params (zero page) */
struct boot_params {
    UINT8   screen_info[64];         /* 0x000 */
    UINT8   apm_bios_info[20];       /* 0x040 */
    UINT8   _pad2[4];                /* 0x054 */
    UINT8   tboot_shared_addr[8];    /* 0x058 */
    UINT8   ist_info[16];            /* 0x060 */
    UINT8   acpi_rsdp_addr[8];       /* 0x070 */
    UINT8   _pad3[8];                /* 0x078 */
    UINT8   hd0_info[16];            /* 0x080 */
    UINT8   hd1_info[16];            /* 0x090 */
    UINT8   sys_desc_table[16];      /* 0x0a0 */
    UINT8   olpc_ofw_header[16];     /* 0x0b0 */
    UINT32  ext_ramdisk_image;       /* 0x0c0 */
    UINT32  ext_ramdisk_size;        /* 0x0c4 */
    UINT32  ext_cmd_line_ptr;        /* 0x0c8 */
    UINT8   _pad4[116];              /* 0x0cc */
    UINT8   edid_info[128];          /* 0x140 */
    UINT8   efi_info[32];            /* 0x1c0 */
    UINT32  alt_mem_k;               /* 0x1e0 */
    UINT32  scratch;                 /* 0x1e4 */
    UINT8   e820_entries;            /* 0x1e8 */
    UINT8   eddbuf_entries;          /* 0x1e9 */
    UINT8   edd_mbr_sig_buf_entries; /* 0x1ea */
    UINT8   kbd_status;              /* 0x1eb */
    UINT8   _pad5[3];                /* 0x1ec */
    UINT8   sentinel;                /* 0x1ef */
    UINT8   _pad6[1];                /* 0x1f0 */
    struct  setup_header hdr;        /* 0x1f1 */
    UINT8   _pad7[0x290 - 0x1f1 - sizeof(struct setup_header)];
    UINT32  edd_mbr_sig_buffer[16];  /* 0x290 */
    struct  boot_e820_entry e820_table[E820_MAX_ENTRIES]; /* 0x2d0 */
    UINT8   _pad8[48];               /* 0xcd0 */
    UINT8   eddbuf[864];             /* 0xd00 */
    UINT8   _pad9[276];              /* 0xeec */
};

#pragma pack(pop)

EFI_STATUS linux_boot(
    EFI_HANDLE image_handle,
    VOID *kernel_buf,
    UINTN kernel_size,
    VOID *initrd_buf,
    UINTN initrd_size,
    CHAR8 *cmdline
);

#endif /* LINUX_BOOT_H */
