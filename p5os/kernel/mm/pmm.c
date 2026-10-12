/* Physical page frame allocator: one bit per 4 KB frame below
 * PMM_MAX_PHYS (64 MB -> 2 KB bitmap in .bss).  Bit set = not available. */
#include <mm.h>
#include <string.h>
#include <klog.h>
#include <io.h>

static u32 bitmap[PMM_MAX_FRAMES / 32];
static u8 owner[PMM_MAX_FRAMES / 8];    /* bit set = frame is allocatable RAM */
static u32 nframes;                     /* frames covered (highest usable) */
static u32 managed, free_frames, allocated, failures, ignored_kb;
static u32 search_hint;

static inline bool test_bit(u32 f) { return bitmap[f >> 5] & (1u << (f & 31)); }
static inline void set_bit(u32 f) { bitmap[f >> 5] |= 1u << (f & 31); }
static inline void clr_bit(u32 f) { bitmap[f >> 5] &= ~(1u << (f & 31)); }
static inline bool is_ram(u32 f) { return owner[f >> 3] & (1u << (f & 7)); }

static void reserve_range(u32 start, u32 end)       /* [start, end) bytes */
{
    for (u32 f = start >> PAGE_SHIFT; f < nframes && f < ALIGN_UP(end, PAGE_SIZE) >> PAGE_SHIFT; f++) {
        if (is_ram(f) && !test_bit(f)) {
            set_bit(f);
            free_frames--;
        }
    }
}

void pmm_init(void)
{
    memset(bitmap, 0xFF, sizeof(bitmap));
    memset(owner, 0, sizeof(owner));
    nframes = managed = free_frames = 0;

    /* 1) free every frame that lies completely inside a usable region */
    for (u32 i = 0; i < memmap_count(); i++) {
        const struct mem_region *r = memmap_get(i);
        if (r->type != E820_USABLE)
            continue;
        u64 start = ALIGN_UP(r->base, (u64)PAGE_SIZE);
        u64 end = ALIGN_DOWN(r->base + r->length, (u64)PAGE_SIZE);
        if (end > PMM_MAX_PHYS) {
            u64 lo = start > PMM_MAX_PHYS ? start : PMM_MAX_PHYS;
            if (end > lo)
                ignored_kb += (u32)((end - lo) >> 10);
            end = PMM_MAX_PHYS;
        }
        for (u64 a = start; a < end; a += PAGE_SIZE) {
            u32 f = (u32)(a >> PAGE_SHIFT);
            if (!is_ram(f)) {
                owner[f >> 3] |= (u8)(1u << (f & 7));
                clr_bit(f);
                managed++;
                free_frames++;
                if (f + 1 > nframes)
                    nframes = f + 1;
            }
        }
    }
    /* 2) any non-usable region overlapping a frame wins (overlapping maps) */
    for (u32 i = 0; i < memmap_count(); i++) {
        const struct mem_region *r = memmap_get(i);
        if (r->type == E820_USABLE || r->base >= PMM_MAX_PHYS)
            continue;
        u64 end = r->base + r->length;
        reserve_range((u32)ALIGN_DOWN(r->base, (u64)PAGE_SIZE),
                      end > PMM_MAX_PHYS ? PMM_MAX_PHYS : (u32)end);
    }
    /* 3) low memory (IVT/BDA/boot data) and the kernel image + bss */
    reserve_range(0, LOW_RESERVED);
    reserve_range((u32)__kernel_start, (u32)__kernel_end);

    LOG_INFO("pmm: %u frames managed (%u KB), %u free, %u reserved%s",
             managed, managed * 4, free_frames, managed - free_frames,
             ignored_kb ? " (RAM above 64 MB ignored)" : "");
}

u32 pmm_alloc(void)
{
    u32 addr = pmm_alloc_contig(1);
    return addr;
}

u32 pmm_alloc_contig(u32 count)
{
    if (!count)
        return 0;
    u32 fl = irq_save();
    for (u32 pass = 0; pass < 2; pass++) {
        u32 start = pass == 0 ? search_hint : 0;
        u32 run = 0;
        for (u32 f = start; f < nframes; f++) {
            run = test_bit(f) ? 0 : run + 1;
            if (run == count) {
                u32 first = f + 1 - count;
                for (u32 i = first; i <= f; i++)
                    set_bit(i);
                free_frames -= count;
                allocated += count;
                search_hint = f + 1;
                irq_restore(fl);
                memset((void *)(first << PAGE_SHIFT), 0, count * PAGE_SIZE);
                return first << PAGE_SHIFT;
            }
        }
    }
    failures++;
    irq_restore(fl);
    LOG_WARN("pmm: out of memory (%u contiguous frame(s) requested, %u free)",
             count, free_frames);
    return 0;
}

void pmm_free_contig(u32 addr, u32 count)
{
    u32 fl = irq_save();
    for (u32 i = 0; i < count; i++) {
        u32 f = (addr >> PAGE_SHIFT) + i;
        if ((addr & (PAGE_SIZE - 1)) || f >= nframes || !is_ram(f) || !test_bit(f)) {
            irq_restore(fl);
            LOG_ERR("pmm: invalid free of frame at %08x", f << PAGE_SHIFT);
            return;
        }
        clr_bit(f);
        free_frames++;
        allocated--;
    }
    if ((addr >> PAGE_SHIFT) < search_hint)
        search_hint = addr >> PAGE_SHIFT;
    irq_restore(fl);
}

void pmm_free(u32 addr) { pmm_free_contig(addr, 1); }

void pmm_stats(struct pmm_stats *s)
{
    s->managed = managed;
    s->free = free_frames;
    s->allocated = allocated;
    s->reserved = managed - free_frames - allocated;
    s->ignored_kb = ignored_kb;
    s->alloc_failures = failures;
}

u32 pmm_highest_frame_end(void) { return nframes << PAGE_SHIFT; }
