/* Boot information block written by boot/stage2.asm at BOOTINFO_ADDR.
 * Field offsets must match the BI_* constants in stage2.asm. */
#ifndef P5_BOOTINFO_H
#define P5_BOOTINFO_H
#include <types.h>

#define BOOTINFO_MAGIC  0x49423550u     /* 'P5BI' */

#define MEM_METHOD_E820 1
#define MEM_METHOD_E801 2
#define MEM_METHOD_88   3

#define E820_USABLE     1
#define E820_RESERVED   2
#define E820_ACPI       3
#define E820_NVS        4
#define E820_BAD        5

struct e820_entry {
    u64 base;
    u64 length;
    u32 type;
    u32 ext_attr;
} PACKED;

struct bootinfo {
    u32 magic;
    u8  boot_drive;         /* BIOS drive number (0x00 floppy, 0x80 HDD) */
    u8  mem_method;         /* MEM_METHOD_* */
    u8  cpuid_present;
    u8  a20_method;         /* 1 already on, 2 BIOS, 3 KBC, 4 port 0x92 */
    u16 lowmem_kb;          /* INT 12h conventional memory */
    u16 e820_count;
    u32 ext_kb_low;         /* E801/88h: KB from 1 MB to 16 MB */
    u32 ext_kb_high;        /* E801: KB above 16 MB */
    u32 e820_addr;          /* physical address of e820_entry array */
    u32 kernel_sectors;
    u32 kernel_lba;
    u16 disk_spt;
    u16 disk_heads;
} PACKED;

STATIC_ASSERT(sizeof(struct e820_entry) == 24, "e820 entry size");
STATIC_ASSERT(sizeof(struct bootinfo) == 36, "bootinfo layout");

#endif
