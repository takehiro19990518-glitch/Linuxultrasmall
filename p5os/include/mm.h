/* Memory management: BIOS memory map, physical page allocator (bitmap),
 * identity-mapped paging with guard pages, kernel heap. */
#ifndef P5_MM_H
#define P5_MM_H
#include <types.h>
#include <bootinfo.h>

#define PAGE_SIZE       4096u
#define PAGE_SHIFT      12
#define LOW_RESERVED    0x10000u        /* IVT, BDA, boot info, E820 buffer */
#define PMM_MAX_PHYS    (64u * 1024 * 1024)   /* frames above this are ignored */
#define PMM_MAX_FRAMES  (PMM_MAX_PHYS / PAGE_SIZE)

/* ---- memory map (kernel/mm/memmap.c) ---- */
#define MEMMAP_MAX      32
struct mem_region {
    u64 base, length;
    u32 type;                           /* E820_* */
};
void memmap_init(const struct bootinfo *bi);    /* copies boot data out of page 0 */
u32 memmap_count(void);
const struct mem_region *memmap_get(u32 i);
const char *memmap_source(void);
const char *memmap_type_name(u32 type);
u32 mem_installed_mb(void);             /* estimate from highest usable address */
u64 mem_usable_bytes(void);

/* ---- physical memory manager (kernel/mm/pmm.c) ---- */
struct pmm_stats {
    u32 managed;        /* frames tracked (usable below PMM_MAX_PHYS) */
    u32 free;
    u32 reserved;       /* kernel image, low memory, page 0 ... */
    u32 allocated;      /* handed out by pmm_alloc* */
    u32 ignored_kb;     /* usable RAM above PMM_MAX_PHYS (not used) */
    u32 alloc_failures;
};
void pmm_init(void);
u32 pmm_alloc(void);                    /* one zeroed frame, 0 on failure */
u32 pmm_alloc_contig(u32 count);        /* physically contiguous, 0 on failure */
void pmm_free(u32 addr);
void pmm_free_contig(u32 addr, u32 count);
void pmm_stats(struct pmm_stats *s);
u32 pmm_highest_frame_end(void);

/* ---- paging (kernel/mm/paging.c) ---- */
void paging_init(void);
bool paging_enabled(void);
u32 paging_table_pages(void);
u32 paging_guard_count(void);
bool paging_is_guard(u32 addr);
bool paging_is_mapped(u32 addr);

/* ---- kernel heap (kernel/mm/heap.c) ---- */
struct heap_stats {
    u32 base, size;
    u32 used_bytes, free_bytes;     /* payload bytes */
    u32 used_blocks, free_blocks;
    u32 largest_free;
    u32 peak_used;
    u32 total_allocs, total_frees, failures;
};
void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *p);
void heap_stats(struct heap_stats *s);
bool heap_check(bool verbose);          /* walk and validate every block */

/* kernel layout symbols (kernel/linker.ld, entry.asm) */
extern u8 __kernel_start[], __image_end[], __bss_start[], __bss_end[], __kernel_end[];
extern u8 kernel_stack_guard[];
#endif
