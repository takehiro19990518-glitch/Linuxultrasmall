/* Paging with 4 KB pages only (no PSE, no PAE: plain i386 two-level paging,
 * fully supported by the P5).  All RAM is identity mapped, so physical ==
 * virtual everywhere in the kernel.  Unmapped on purpose:
 *   - page 0           (NULL pointer dereferences fault)
 *   - the page below the kernel stack (stack overflow faults)
 *   - everything above the top of RAM */
#include <mm.h>
#include <string.h>
#include <klog.h>
#include <panic.h>
#include <arch.h>

#define PTE_PRESENT     0x001
#define PTE_WRITE       0x002
#define PTE_PWT         0x008
#define PTE_PCD         0x010           /* cache disable */
#define CR0_PG          0x80000000u
#define ENTRIES         1024
#define TABLE_SPAN      (ENTRIES * PAGE_SIZE)   /* 4 MB per page table */
#define ADAPTER_START   0xA0000u        /* VGA memory + option/BIOS ROMs */
#define ADAPTER_END     0x100000u
#define MAX_GUARDS      4

static u32 *page_dir;
static u32 tables;
static bool enabled;
static u32 guards[MAX_GUARDS];
static u32 nguards;

static u32 *pte_for(u32 addr)
{
    u32 pde = page_dir[addr >> 22];
    if (!(pde & PTE_PRESENT))
        return NULL;
    return &((u32 *)(pde & ~0xFFFu))[(addr >> PAGE_SHIFT) & (ENTRIES - 1)];
}

static void unmap_page(u32 addr)
{
    u32 *pte = pte_for(addr);
    if (pte)
        *pte = 0;
    if (enabled)
        __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

static void add_guard(u32 addr)
{
    unmap_page(addr);
    if (nguards < MAX_GUARDS)
        guards[nguards++] = addr;
}

void paging_init(void)
{
    u32 ram_end = pmm_highest_frame_end();
    if (ram_end < ADAPTER_END)
        ram_end = ADAPTER_END;
    u32 ntables = ALIGN_UP(ram_end, TABLE_SPAN) / TABLE_SPAN;

    page_dir = (u32 *)pmm_alloc();
    if (!page_dir)
        panic("paging: no memory for the page directory");
    for (u32 t = 0; t < ntables; t++) {
        u32 *pt = (u32 *)pmm_alloc();
        if (!pt)
            panic("paging: no memory for page table %u", t);
        tables++;
        for (u32 i = 0; i < ENTRIES; i++) {
            u32 addr = t * TABLE_SPAN + i * PAGE_SIZE;
            u32 flags = PTE_PRESENT | PTE_WRITE;
            if (addr >= ram_end)
                flags = 0;
            else if (addr >= ADAPTER_START && addr < ADAPTER_END)
                flags |= PTE_PCD | PTE_PWT;     /* device memory: uncached */
            pt[i] = flags ? (addr | flags) : 0;
        }
        page_dir[t] = (u32)pt | PTE_PRESENT | PTE_WRITE;
    }

    add_guard(0);
    add_guard((u32)kernel_stack_guard);

    gdt_set_df_cr3((u32)page_dir);
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir) : "memory");
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG;
    __asm__ volatile("mov %0, %%cr0\n\tjmp 1f\n1:" :: "r"(cr0) : "memory");
    enabled = true;
    LOG_INFO("paging: identity map 0-%08x, %u page table(s), guard pages at 0 and %08x",
             ram_end, tables, (u32)kernel_stack_guard);
}

bool paging_enabled(void) { return enabled; }
u32 paging_table_pages(void) { return tables + (page_dir ? 1 : 0); }
u32 paging_guard_count(void) { return nguards; }

bool paging_is_guard(u32 addr)
{
    for (u32 i = 0; i < nguards; i++)
        if (addr >= guards[i] && addr < guards[i] + PAGE_SIZE)
            return true;
    return false;
}

bool paging_is_mapped(u32 addr)
{
    if (!enabled)
        return true;
    u32 *pte = pte_for(addr);
    return pte && (*pte & PTE_PRESENT);
}
