/* Kernel heap: first-fit allocator over one contiguous region taken from
 * the PMM at boot.  Each block has a 16-byte header with a magic value
 * (detects corruption and double frees), its payload size and a pointer to
 * the previous block (for coalescing).  Payloads are 16-byte aligned. */
#include <mm.h>
#include <string.h>
#include <klog.h>
#include <panic.h>
#include <io.h>
#include <console.h>
#include <printf.h>

#define HEAP_DEFAULT_SIZE   (1024u * 1024u)     /* 1 MB on a 16 MB machine */
#define HEAP_MIN_SIZE       (64u * 1024u)
#define HEAP_ALIGN          16u
#define MAGIC_USED          0xA110CA7Eu
#define MAGIC_FREE          0xF4EEB10Cu
#define MIN_SPLIT           (sizeof(struct block) + HEAP_ALIGN)

struct block {
    u32 magic;
    u32 size;                   /* payload bytes (multiple of HEAP_ALIGN) */
    struct block *prev;         /* previous block in address order */
    u32 reserved;
};
STATIC_ASSERT(sizeof(struct block) == 16, "heap header must stay 16 bytes");

static u8 *heap_base, *heap_end;
static u32 used_bytes, peak_used, total_allocs, total_frees, failures;

static inline struct block *next_block(struct block *b)
{
    u8 *n = (u8 *)b + sizeof(struct block) + b->size;
    return n < heap_end ? (struct block *)n : NULL;
}

static NORETURN void corrupt(struct block *b, const char *what)
{
    panic("heap corruption: %s (block %p magic %08x size %u)", what, (void *)b,
          b->magic, b->size);
}

void heap_init(void)
{
    struct pmm_stats ps;
    pmm_stats(&ps);
    u32 size = HEAP_DEFAULT_SIZE;
    if (size > ps.free * PAGE_SIZE / 4)          /* never more than 1/4 of RAM */
        size = ALIGN_DOWN(ps.free * PAGE_SIZE / 4, PAGE_SIZE);
    if (size < HEAP_MIN_SIZE)
        panic("heap: not enough memory (%u KB free)", ps.free * 4);
    u32 base = pmm_alloc_contig(size / PAGE_SIZE);
    if (!base)
        panic("heap: cannot reserve %u KB of contiguous memory", size / 1024);
    heap_base = (u8 *)base;
    heap_end = heap_base + size;
    struct block *b = (struct block *)heap_base;
    b->magic = MAGIC_FREE;
    b->size = size - sizeof(struct block);
    b->prev = NULL;
    LOG_INFO("heap: %u KB at %08x", size / 1024, base);
}

void *kmalloc(size_t size)
{
    if (size == 0 || !heap_base || size > (u32)(heap_end - heap_base))
        return NULL;
    size = ALIGN_UP(size, HEAP_ALIGN);
    u32 fl = irq_save();
    for (struct block *b = (struct block *)heap_base; b; b = next_block(b)) {
        if (b->magic != MAGIC_FREE && b->magic != MAGIC_USED)
            corrupt(b, "bad magic during allocation");
        if (b->magic != MAGIC_FREE || b->size < size)
            continue;
        if (b->size >= size + MIN_SPLIT) {       /* split off the remainder */
            struct block *rest = (struct block *)((u8 *)b + sizeof(struct block) + size);
            rest->magic = MAGIC_FREE;
            rest->size = b->size - size - sizeof(struct block);
            rest->prev = b;
            struct block *after = next_block(rest);
            if (after)
                after->prev = rest;
            b->size = size;
        }
        b->magic = MAGIC_USED;
        used_bytes += b->size;
        if (used_bytes > peak_used)
            peak_used = used_bytes;
        total_allocs++;
        irq_restore(fl);
        return b + 1;
    }
    failures++;
    irq_restore(fl);
    LOG_WARN("kmalloc: out of memory (%u bytes requested, %u in use)", size, used_bytes);
    return NULL;
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void kfree(void *p)
{
    if (!p)
        return;
    struct block *b = (struct block *)p - 1;
    if ((u8 *)b < heap_base || (u8 *)p >= heap_end || ((u32)p & (HEAP_ALIGN - 1)))
        panic("kfree: pointer %p is not a heap block", p);
    u32 fl = irq_save();
    if (b->magic == MAGIC_FREE)
        corrupt(b, "double free");
    if (b->magic != MAGIC_USED)
        corrupt(b, "bad magic in kfree");
    struct block *n = next_block(b);
    if (n && n->magic != MAGIC_USED && n->magic != MAGIC_FREE)
        corrupt(n, "next block damaged (buffer overrun?)");

    used_bytes -= b->size;
    total_frees++;
    b->magic = MAGIC_FREE;
    /* merge with the following block */
    if (n && n->magic == MAGIC_FREE) {
        b->size += sizeof(struct block) + n->size;
        n->magic = 0;
        struct block *nn = next_block(b);
        if (nn)
            nn->prev = b;
    }
    /* merge into the preceding block */
    if (b->prev && b->prev->magic == MAGIC_FREE) {
        struct block *pb = b->prev;
        pb->size += sizeof(struct block) + b->size;
        b->magic = 0;
        struct block *nn = next_block(pb);
        if (nn)
            nn->prev = pb;
    }
    irq_restore(fl);
}

void heap_stats(struct heap_stats *s)
{
    memset(s, 0, sizeof(*s));
    s->base = (u32)heap_base;
    s->size = (u32)(heap_end - heap_base);
    u32 fl = irq_save();
    for (struct block *b = (struct block *)heap_base; b; b = next_block(b)) {
        if (b->magic == MAGIC_USED) {
            s->used_blocks++;
            s->used_bytes += b->size;
        } else {
            s->free_blocks++;
            s->free_bytes += b->size;
            if (b->size > s->largest_free)
                s->largest_free = b->size;
        }
    }
    irq_restore(fl);
    s->peak_used = peak_used;
    s->total_allocs = total_allocs;
    s->total_frees = total_frees;
    s->failures = failures;
}

/* Full consistency walk: magic values, prev links, exact coverage of the
 * region, no two adjacent free blocks. */
bool heap_check(bool verbose)
{
    u32 fl = irq_save();
    struct block *prev = NULL;
    bool ok = true, prev_free = false;
    u8 *p = heap_base;
    const char *err = NULL;
    while (p < heap_end) {
        struct block *b = (struct block *)p;
        if (b->magic != MAGIC_USED && b->magic != MAGIC_FREE) { err = "bad magic"; break; }
        if (b->prev != prev) { err = "broken prev link"; break; }
        if (b->size & (HEAP_ALIGN - 1)) { err = "misaligned size"; break; }
        if (p + sizeof(struct block) + b->size > heap_end) { err = "block overruns heap"; break; }
        bool is_free = b->magic == MAGIC_FREE;
        if (is_free && prev_free) { err = "uncoalesced free blocks"; break; }
        prev_free = is_free;
        prev = b;
        p += sizeof(struct block) + b->size;
    }
    irq_restore(fl);
    if (err) {
        ok = false;
        LOG_ERR("heap check failed at %p: %s", (void *)p, err);
    } else if (p != heap_end) {
        ok = false;
        LOG_ERR("heap check failed: walk ended at %p, heap end %p", (void *)p, (void *)heap_end);
    }
    if (verbose)
        kprintf("Heap check: %s\n", ok ? "OK" : "FAILED (see dmesg)");
    return ok;
}
