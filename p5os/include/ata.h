/* ATA (IDE) hard disks, PIO mode, polled (no IRQ, no DMA). */
#ifndef P5_ATA_H
#define P5_ATA_H
#include <types.h>

#define ATA_SECTOR_SIZE 512
#define ATA_MAX_DEVICES 4       /* primary/secondary x master/slave */

struct ata_device {
    bool present;
    bool lba;                   /* LBA28 supported (else CHS) */
    bool flush_supported;
    u8 channel, drive;          /* 0/1, 0 = master / 1 = slave */
    u16 io, ctrl;
    char name[4];               /* "hd0".."hd3" */
    char model[41];
    char serial[21];
    u32 sectors;                /* addressable sectors */
    u16 cylinders, heads, spt;  /* CHS geometry (IDENTIFY words 1,3,6) */
    u32 reads, writes;          /* sectors transferred */
    u32 errors;
};

int ata_init(void);             /* probe both channels, returns device count */
struct ata_device *ata_get(u32 index);
struct ata_device *ata_find(const char *name);
int ata_read(struct ata_device *d, u32 lba, u32 count, void *buf);
int ata_write(struct ata_device *d, u32 lba, u32 count, const void *buf);
#endif
