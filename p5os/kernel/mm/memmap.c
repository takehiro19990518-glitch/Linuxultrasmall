/* Physical memory map from the BIOS (via stage 2).  E820 is preferred; the
 * E801 / AH=88h fallbacks are turned into an equivalent region list. */
#include <mm.h>
#include <string.h>
#include <klog.h>

#define MB              (1024u * 1024u)
#define KB              1024u
#define STD_LOWMEM_KB   640u

static struct mem_region regions[MEMMAP_MAX];
static u32 nregions;
static const char *source = "none";

static void add(u64 base, u64 len, u32 type)
{
    if (!len || nregions >= MEMMAP_MAX)
        return;
    regions[nregions].base = base;
    regions[nregions].length = len;
    regions[nregions].type = type;
    nregions++;
}

void memmap_init(const struct bootinfo *bi)
{
    nregions = 0;
    if (bi->mem_method == MEM_METHOD_E820 && bi->e820_count) {
        const struct e820_entry *e = (const struct e820_entry *)bi->e820_addr;
        u32 n = MIN((u32)bi->e820_count, (u32)MEMMAP_MAX);
        for (u32 i = 0; i < n; i++)
            add(e[i].base, e[i].length, e[i].type);
        source = "BIOS INT 15h E820";
    } else {
        u32 low = bi->lowmem_kb ? bi->lowmem_kb : STD_LOWMEM_KB;
        add(0, (u64)low * KB, E820_USABLE);
        add(1 * MB, (u64)bi->ext_kb_low * KB, E820_USABLE);
        if (bi->mem_method == MEM_METHOD_E801) {
            add(16 * MB, (u64)bi->ext_kb_high * KB, E820_USABLE);
            source = "BIOS INT 15h E801";
        } else {
            source = "BIOS INT 15h AH=88h";
        }
    }
    /* sort by base address (insertion sort, tiny list) */
    for (u32 i = 1; i < nregions; i++) {
        struct mem_region r = regions[i];
        u32 j = i;
        while (j > 0 && regions[j - 1].base > r.base) {
            regions[j] = regions[j - 1];
            j--;
        }
        regions[j] = r;
    }
}

u32 memmap_count(void) { return nregions; }
const struct mem_region *memmap_get(u32 i) { return i < nregions ? &regions[i] : NULL; }
const char *memmap_source(void) { return source; }

const char *memmap_type_name(u32 type)
{
    switch (type) {
    case E820_USABLE:   return "usable";
    case E820_RESERVED: return "reserved";
    case E820_ACPI:     return "ACPI data";
    case E820_NVS:      return "ACPI NVS";
    case E820_BAD:      return "bad RAM";
    default:            return "unknown";
    }
}

u64 mem_usable_bytes(void)
{
    u64 total = 0;
    for (u32 i = 0; i < nregions; i++)
        if (regions[i].type == E820_USABLE)
            total += regions[i].length;
    return total;
}

/* Installed RAM ~ end of the highest usable region, rounded up to 1 MB
 * (BIOSes reserve a little at the top, e.g. for ACPI tables). */
u32 mem_installed_mb(void)
{
    u64 top = 0;
    for (u32 i = 0; i < nregions; i++)
        if (regions[i].type == E820_USABLE && regions[i].base + regions[i].length > top)
            top = regions[i].base + regions[i].length;
    return (u32)((top + MB - 1) >> 20);
}
