/* Memory commands: mem, memtest */
#include <shell.h>
#include <mm.h>
#include <arch.h>
#include <printf.h>
#include <string.h>
#include <console.h>
#include <timer.h>

#define KB(x)   ((x) / 1024u)

static void print_map(void)
{
    kprintf("Physical memory map (%s):\n", memmap_source());
    for (u32 i = 0; i < memmap_count(); i++) {
        const struct mem_region *r = memmap_get(i);
        u64 end = r->base + r->length - 1;
        kprintf("  %08llx-%08llx  %-9s %8llu KB\n", r->base, end,
                memmap_type_name(r->type), r->length >> 10);
    }
}

static int cmd_mem(int argc, char **argv, const char *rest)
{
    if (argc > 1 && !strcmp(argv[1], "map")) {
        print_map();
        return 0;
    }
    struct pmm_stats ps;
    struct heap_stats hs;
    pmm_stats(&ps);
    heap_stats(&hs);
    u32 image = (u32)(__image_end - __kernel_start);
    u32 bss = (u32)(__bss_end - __bss_start);
    u32 kern = (u32)(__kernel_end - __kernel_start);

    kprintf("Installed RAM : %u MB (%llu KB usable per %s)\n", mem_installed_mb(),
            mem_usable_bytes() >> 10, memmap_source());
    kprintf("Page frames   : %u total (%u KB): %u free, %u allocated, %u reserved\n",
            ps.managed, ps.managed * 4, ps.free, ps.allocated, ps.reserved);
    if (ps.ignored_kb)
        kprintf("                %u KB above 64 MB not used\n", ps.ignored_kb);
    kprintf("Kernel        : %08x-%08x %u KB (code+data %u KB, bss %u KB)\n",
            (u32)__kernel_start, (u32)__kernel_end, KB(kern), KB(image), KB(bss));
    kprintf("Kernel stack  : %u KB, peak use %u bytes, canary %s, guard page %08x\n",
            KB((u32)(kernel_stack_top - kernel_stack_bottom)), stack_usage_peak(),
            stack_guard_ok() ? "ok" : "DAMAGED", (u32)kernel_stack_guard);
    kprintf("Paging        : %s, %u page-table pages (%u KB), %u guard pages\n",
            paging_enabled() ? "on (4 KB pages, identity)" : "off",
            paging_table_pages(), paging_table_pages() * 4, paging_guard_count());
    kprintf("Kernel heap   : %u KB at %08x: used %u B in %u blocks, free %u B (largest %u B)\n",
            KB(hs.size), hs.base, hs.used_bytes, hs.used_blocks, hs.free_bytes, hs.largest_free);
    kprintf("                peak %u B, %u allocs, %u frees, %u failures\n",
            hs.peak_used, hs.total_allocs, hs.total_frees, hs.failures);
    kprintf("Free memory   : %u KB of %u KB managed\n", ps.free * 4, ps.managed * 4);
    return 0;
}

/* simple LCG so the test pattern is deterministic */
static u32 rng = 12345;
static u32 rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

#define TEST_SLOTS 64

static bool heap_test(void)
{
    static void *ptr[TEST_SLOTS];
    static u32 len[TEST_SLOTS];
    struct heap_stats before, after;
    heap_stats(&before);
    memset(ptr, 0, sizeof(ptr));
    bool ok = true;
    for (u32 round = 0; round < 2000 && ok; round++) {
        u32 i = rnd() % TEST_SLOTS;
        if (ptr[i]) {
            u8 *p = ptr[i];
            for (u32 j = 0; j < len[i]; j++)
                if (p[j] != (u8)(i + j)) {
                    kprintf("  heap: data mismatch in slot %u\n", i);
                    ok = false;
                    break;
                }
            kfree(ptr[i]);
            ptr[i] = NULL;
        } else {
            len[i] = 1 + rnd() % 4000;
            ptr[i] = kmalloc(len[i]);
            if (!ptr[i]) {
                kprintf("  heap: allocation of %u bytes failed\n", len[i]);
                ok = false;
                break;
            }
            for (u32 j = 0; j < len[i]; j++)
                ((u8 *)ptr[i])[j] = (u8)(i + j);
        }
    }
    for (u32 i = 0; i < TEST_SLOTS; i++)
        kfree(ptr[i]);
    heap_stats(&after);
    if (after.used_bytes != before.used_bytes || after.used_blocks != before.used_blocks) {
        kprintf("  heap: leak detected (%u -> %u bytes)\n", before.used_bytes, after.used_bytes);
        ok = false;
    }
    if (!heap_check(false))
        ok = false;
    kprintf("  heap random alloc/free (2000 ops): %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool page_test(void)
{
    struct pmm_stats before, after;
    pmm_stats(&before);
    u32 n = 0, pages[256];
    bool ok = true;
    while (n < ARRAY_SIZE(pages)) {
        u32 a = pmm_alloc();
        if (!a)
            break;
        pages[n++] = a;
        for (u32 *w = (u32 *)a; w < (u32 *)(a + PAGE_SIZE); w++)
            *w = a ^ (u32)w;
    }
    for (u32 i = 0; i < n; i++) {
        for (u32 *w = (u32 *)pages[i]; w < (u32 *)(pages[i] + PAGE_SIZE); w++)
            if (*w != (pages[i] ^ (u32)w)) {
                ok = false;
                break;
            }
        pmm_free(pages[i]);
    }
    pmm_stats(&after);
    if (after.free != before.free)
        ok = false;
    kprintf("  page frames write/verify (%u pages = %u KB): %s\n", n, n * 4,
            ok ? "PASS" : "FAIL");
    return ok;
}

/* Exhaust the heap on purpose and check that failure is reported cleanly. */
static bool oom_test(void)
{
    struct heap_stats hs;
    void *head = NULL;
    u32 count = 0;
    for (;;) {
        void **p = kmalloc(16 * 1024);
        if (!p)
            break;
        *p = head;
        head = p;
        count++;
    }
    heap_stats(&hs);
    bool detected = hs.failures > 0;
    while (head) {
        void *next = *(void **)head;
        kfree(head);
        head = next;
    }
    bool ok = detected && heap_check(false);
    kprintf("  out-of-memory: %u x 16 KB allocated before NULL, recovered: %s\n",
            count, ok ? "PASS" : "FAIL");
    return ok;
}

static int cmd_memtest(int argc, char **argv, const char *rest)
{
    const char *what = argc > 1 ? argv[1] : "all";
    bool ok = true, all = !strcmp(what, "all");
    if (all || !strcmp(what, "heap"))
        ok &= heap_test();
    if (all || !strcmp(what, "pages"))
        ok &= page_test();
    if (all || !strcmp(what, "oom"))
        ok &= oom_test();
    if (!all && strcmp(what, "heap") && strcmp(what, "pages") && strcmp(what, "oom")) {
        kprintf("usage: memtest [all|heap|pages|oom]\n");
        return 1;
    }
    kprintf("memtest: %s\n", ok ? "all tests passed" : "FAILURES");
    return ok ? 0 : 1;
}

const struct shell_cmd mem_commands[] = {
    { "mem",     "[map]",   "memory usage / BIOS memory map", cmd_mem },
    { "memtest", "[kind]",  "heap/page/out-of-memory self test", cmd_memtest },
    { NULL, NULL, NULL, NULL },
};
