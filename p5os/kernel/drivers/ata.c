/* ATA PIO driver (polling).  Works with LBA28 drives and falls back to CHS
 * addressing for very old drives without LBA support.  Interrupts from the
 * drives are disabled (nIEN) and IRQ 14/15 stay masked at the PIC. */
#include <ata.h>
#include <errno.h>
#include <io.h>
#include <klog.h>
#include <string.h>
#include <timer.h>

/* task-file register offsets from the I/O base */
#define REG_DATA        0
#define REG_ERROR       1
#define REG_FEATURES    1
#define REG_SECCOUNT    2
#define REG_LBA0        3       /* sector number in CHS mode */
#define REG_LBA1        4       /* cylinder low */
#define REG_LBA2        5       /* cylinder high */
#define REG_DRIVE       6       /* drive/head */
#define REG_STATUS      7
#define REG_COMMAND     7
/* control block (ctrl base) */
#define REG_ALTSTATUS   0
#define REG_DEVCTRL     0

#define ST_ERR          0x01
#define ST_DRQ          0x08
#define ST_DF           0x20
#define ST_DRDY         0x40
#define ST_BSY          0x80

#define ER_AMNF         0x01
#define ER_ABRT         0x04
#define ER_IDNF         0x10
#define ER_UNC          0x40
#define ER_BBK          0x80

#define CTL_NIEN        0x02    /* disable device interrupts */
#define CTL_SRST        0x04    /* software reset */

#define DRV_BASE        0xA0    /* bits 7 and 5 always set */
#define DRV_LBA         0x40
#define DRV_SLAVE       0x10

#define CMD_READ        0x20    /* READ SECTORS (PIO, with retries) */
#define CMD_WRITE       0x30    /* WRITE SECTORS */
#define CMD_FLUSH       0xE7    /* FLUSH CACHE (ATA-4+, may be aborted) */
#define CMD_IDENTIFY    0xEC

#define TIMEOUT_RESET_MS    5000
#define TIMEOUT_CMD_MS      5000
#define TIMEOUT_FLUSH_MS    30000
#define MAX_SECTORS_PER_CMD 128
#define RETRIES             2
#define LBA28_LIMIT         0x10000000u

static const u16 chan_io[2] = { 0x1F0, 0x170 };
static const u16 chan_ctrl[2] = { 0x3F6, 0x376 };
static struct ata_device devs[ATA_MAX_DEVICES];

/* ~400 ns: four reads of the alternate status register */
static void delay400(u16 ctrl)
{
    for (int i = 0; i < 4; i++)
        (void)inb(ctrl + REG_ALTSTATUS);
}

/* Wait for BSY to clear.  Returns final status, or -1 on timeout. */
static int wait_not_busy(u16 io, u32 timeout_ms)
{
    u64 end = timer_uptime_ms() + timeout_ms;
    for (;;) {
        u8 st = inb(io + REG_STATUS);
        if (!(st & ST_BSY))
            return st;
        if (timer_uptime_ms() > end)
            return -1;
    }
}

/* Wait for BSY clear and then DRQ (or an error). */
static int wait_drq(struct ata_device *d, u32 timeout_ms)
{
    int st = wait_not_busy(d->io, timeout_ms);
    if (st < 0)
        return E_TIMEOUT;
    if (st & (ST_ERR | ST_DF))
        return E_IO;
    if (!(st & ST_DRQ))
        return E_IO;
    return E_OK;
}

static void select_drive(struct ata_device *d, u8 head_bits)
{
    outb(d->io + REG_DRIVE, (u8)(DRV_BASE | (d->drive ? DRV_SLAVE : 0) | head_bits));
    delay400(d->ctrl);
}

static void log_error(struct ata_device *d, const char *op, u32 lba, int rc)
{
    u8 st = inb(d->io + REG_STATUS), er = inb(d->io + REG_ERROR);
    d->errors++;
    LOG_ERR("ata %s: %s error at LBA %u: %s (status %02x error %02x%s%s%s%s%s)", d->name, op,
            lba, rc == E_TIMEOUT ? "timeout" : "device error", st, er,
            (er & ER_BBK) ? " BBK" : "", (er & ER_UNC) ? " UNC" : "",
            (er & ER_IDNF) ? " IDNF" : "", (er & ER_ABRT) ? " ABRT" : "",
            (er & ER_AMNF) ? " AMNF" : "");
}

static void copy_string(char *dst, const u16 *words, u32 nwords)
{
    /* IDENTIFY strings are big-endian byte pairs, space padded */
    for (u32 i = 0; i < nwords; i++) {
        dst[2 * i] = (char)(words[i] >> 8);
        dst[2 * i + 1] = (char)words[i];
    }
    dst[2 * nwords] = '\0';
    for (int i = (int)(2 * nwords) - 1; i >= 0 && dst[i] == ' '; i--)
        dst[i] = '\0';
    char *s = dst;
    while (*s == ' ')
        s++;
    memmove(dst, s, strlen(s) + 1);
}

static bool identify(struct ata_device *d)
{
    u16 id[256];
    select_drive(d, 0);
    outb(d->io + REG_SECCOUNT, 0);
    outb(d->io + REG_LBA0, 0);
    outb(d->io + REG_LBA1, 0);
    outb(d->io + REG_LBA2, 0);
    outb(d->io + REG_COMMAND, CMD_IDENTIFY);
    delay400(d->ctrl);
    if (inb(d->io + REG_STATUS) == 0)
        return false;                       /* no device */
    if (wait_not_busy(d->io, TIMEOUT_CMD_MS) < 0)
        return false;
    u8 sig1 = inb(d->io + REG_LBA1), sig2 = inb(d->io + REG_LBA2);
    if (sig1 || sig2) {
        LOG_INFO("ata %s: non-ATA device (signature %02x%02x, ATAPI?) ignored",
                 d->name, sig2, sig1);
        return false;
    }
    if (wait_drq(d, TIMEOUT_CMD_MS) != E_OK)
        return false;
    insw(d->io + REG_DATA, id, 256);

    d->cylinders = id[1];
    d->heads = id[3];
    d->spt = id[6];
    d->lba = (id[49] & (1 << 9)) != 0;
    u32 lba_sectors = id[60] | ((u32)id[61] << 16);
    d->sectors = d->lba ? lba_sectors : (u32)d->cylinders * d->heads * d->spt;
    if (d->sectors > LBA28_LIMIT)
        d->sectors = LBA28_LIMIT;           /* LBA48 not implemented */
    copy_string(d->model, &id[27], 20);
    copy_string(d->serial, &id[10], 10);
    d->flush_supported = true;              /* cleared if the drive aborts it */
    if (!d->sectors || (!d->lba && (!d->heads || !d->spt)))
        return false;
    return true;
}

static void reset_channel(u32 ch)
{
    outb(chan_ctrl[ch] + REG_DEVCTRL, CTL_NIEN | CTL_SRST);
    for (int i = 0; i < 10; i++)
        io_wait();                          /* >= 5 us */
    outb(chan_ctrl[ch] + REG_DEVCTRL, CTL_NIEN);
    timer_sleep_ms(2);
    if (wait_not_busy(chan_io[ch], TIMEOUT_RESET_MS) < 0)
        LOG_WARN("ata: channel %u stays busy after reset", ch);
}

int ata_init(void)
{
    int found = 0;
    for (u32 ch = 0; ch < 2; ch++) {
        /* 0xFF = floating bus: no controller or no drives on this channel */
        if (inb(chan_io[ch] + REG_STATUS) == 0xFF) {
            LOG_INFO("ata: channel %u: no controller/drives", ch);
            continue;
        }
        reset_channel(ch);
        for (u32 dr = 0; dr < 2; dr++) {
            struct ata_device *d = &devs[ch * 2 + dr];
            memset(d, 0, sizeof(*d));
            d->channel = (u8)ch;
            d->drive = (u8)dr;
            d->io = chan_io[ch];
            d->ctrl = chan_ctrl[ch];
            d->name[0] = 'h';
            d->name[1] = 'd';
            d->name[2] = (char)('0' + ch * 2 + dr);
            if (identify(d)) {
                d->present = true;
                found++;
                LOG_INFO("ata %s: \"%s\" %u sectors (%u MB), %s, CHS %u/%u/%u",
                         d->name, d->model, d->sectors, d->sectors / 2048,
                         d->lba ? "LBA" : "CHS only", d->cylinders, d->heads, d->spt);
            }
        }
    }
    return found;
}

struct ata_device *ata_get(u32 index)
{
    return index < ATA_MAX_DEVICES && devs[index].present ? &devs[index] : NULL;
}

struct ata_device *ata_find(const char *name)
{
    for (u32 i = 0; i < ATA_MAX_DEVICES; i++)
        if (devs[i].present && !strcmp(devs[i].name, name))
            return &devs[i];
    return NULL;
}

/* Program the task file for one command of 'count' sectors at 'lba'. */
static int setup(struct ata_device *d, u32 lba, u32 count, u8 cmd)
{
    if (wait_not_busy(d->io, TIMEOUT_CMD_MS) < 0)
        return E_TIMEOUT;
    if (d->lba) {
        select_drive(d, (u8)(DRV_LBA | ((lba >> 24) & 0x0F)));
        outb(d->io + REG_SECCOUNT, (u8)count);      /* 0 would mean 256 */
        outb(d->io + REG_LBA0, (u8)lba);
        outb(d->io + REG_LBA1, (u8)(lba >> 8));
        outb(d->io + REG_LBA2, (u8)(lba >> 16));
    } else {
        u32 cyl = lba / ((u32)d->heads * d->spt);
        u32 head = (lba / d->spt) % d->heads;
        u32 sec = lba % d->spt + 1;
        select_drive(d, (u8)(head & 0x0F));
        outb(d->io + REG_SECCOUNT, (u8)count);
        outb(d->io + REG_LBA0, (u8)sec);
        outb(d->io + REG_LBA1, (u8)cyl);
        outb(d->io + REG_LBA2, (u8)(cyl >> 8));
    }
    if (wait_not_busy(d->io, TIMEOUT_CMD_MS) < 0)
        return E_TIMEOUT;
    outb(d->io + REG_COMMAND, cmd);
    return E_OK;
}

static int read_chunk(struct ata_device *d, u32 lba, u32 count, u8 *buf)
{
    int rc = setup(d, lba, count, CMD_READ);
    if (rc)
        return rc;
    for (u32 i = 0; i < count; i++) {
        delay400(d->ctrl);
        rc = wait_drq(d, TIMEOUT_CMD_MS);
        if (rc)
            return rc;
        insw(d->io + REG_DATA, buf + i * ATA_SECTOR_SIZE, ATA_SECTOR_SIZE / 2);
    }
    delay400(d->ctrl);
    int st = wait_not_busy(d->io, TIMEOUT_CMD_MS);
    if (st < 0)
        return E_TIMEOUT;
    return (st & (ST_ERR | ST_DF)) ? E_IO : E_OK;
}

static int write_chunk(struct ata_device *d, u32 lba, u32 count, const u8 *buf)
{
    int rc = setup(d, lba, count, CMD_WRITE);
    if (rc)
        return rc;
    for (u32 i = 0; i < count; i++) {
        delay400(d->ctrl);
        rc = wait_drq(d, TIMEOUT_CMD_MS);
        if (rc)
            return rc;
        outsw(d->io + REG_DATA, buf + i * ATA_SECTOR_SIZE, ATA_SECTOR_SIZE / 2);
    }
    delay400(d->ctrl);
    int st = wait_not_busy(d->io, TIMEOUT_CMD_MS);
    if (st < 0)
        return E_TIMEOUT;
    return (st & (ST_ERR | ST_DF)) ? E_IO : E_OK;
}

/* Make sure written data reaches the platters.  Drives older than ATA-4
 * abort FLUSH CACHE; they have no write-back cache to flush, so that is
 * not treated as an error. */
static int flush(struct ata_device *d)
{
    if (!d->flush_supported)
        return E_OK;
    if (wait_not_busy(d->io, TIMEOUT_CMD_MS) < 0)
        return E_TIMEOUT;
    select_drive(d, d->lba ? DRV_LBA : 0);
    outb(d->io + REG_COMMAND, CMD_FLUSH);
    delay400(d->ctrl);
    int st = wait_not_busy(d->io, TIMEOUT_FLUSH_MS);
    if (st < 0)
        return E_TIMEOUT;
    if (st & ST_ERR) {
        if (inb(d->io + REG_ERROR) & ER_ABRT) {
            d->flush_supported = false;
            LOG_INFO("ata %s: FLUSH CACHE not supported (pre-ATA-4 drive)", d->name);
            return E_OK;
        }
        return E_IO;
    }
    return (st & ST_DF) ? E_IO : E_OK;
}

static int transfer(struct ata_device *d, u32 lba, u32 count, void *buf, bool write)
{
    if (!d || !d->present)
        return E_NODEV;
    if (count == 0 || lba >= d->sectors || count > d->sectors - lba)
        return E_RANGE;
    u8 *p = buf;
    while (count) {
        u32 n = MIN(count, (u32)MAX_SECTORS_PER_CMD);
        int rc = E_IO;
        for (int attempt = 0; attempt <= RETRIES; attempt++) {
            rc = write ? write_chunk(d, lba, n, p) : read_chunk(d, lba, n, p);
            if (rc == E_OK)
                break;
            log_error(d, write ? "write" : "read", lba, rc);
            if (rc == E_TIMEOUT)
                reset_channel(d->channel);
        }
        if (rc)
            return rc;
        if (write)
            d->writes += n;
        else
            d->reads += n;
        lba += n;
        count -= n;
        p += n * ATA_SECTOR_SIZE;
    }
    if (write) {
        int rc = flush(d);
        if (rc) {
            log_error(d, "flush", lba, rc);
            return rc;
        }
    }
    return E_OK;
}

int ata_read(struct ata_device *d, u32 lba, u32 count, void *buf)
{
    return transfer(d, lba, count, buf, false);
}

int ata_write(struct ata_device *d, u32 lba, u32 count, const void *buf)
{
    return transfer(d, lba, count, (void *)buf, true);
}
