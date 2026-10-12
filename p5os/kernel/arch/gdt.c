/* Global Descriptor Table: flat 4 GB code/data segments (ring 0 only) and
 * two TSSs.  The second TSS is used by a task gate for the double-fault
 * vector, so a kernel stack overflow still produces a readable panic
 * instead of a silent triple-fault reset. */
#include <arch.h>
#include <string.h>

struct gdt_entry {
    u16 limit_lo;
    u16 base_lo;
    u8  base_mid;
    u8  access;
    u8  flags_limit_hi;
    u8  base_hi;
} PACKED;

struct gdt_ptr {
    u16 limit;
    u32 base;
} PACKED;

/* 32-bit TSS (Intel SDM vol. 3, 7.2.1) */
struct tss {
    u32 prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap_base;
} PACKED;

#define GDT_ENTRIES     5
#define ACC_CODE        0x9A    /* present, ring 0, code, readable */
#define ACC_DATA        0x92    /* present, ring 0, data, writable */
#define ACC_TSS         0x89    /* present, ring 0, 32-bit available TSS */
#define FLAGS_4K_32     0xC0    /* 4 KB granularity, 32-bit */
#define DF_STACK_SIZE   4096

static struct gdt_entry gdt[GDT_ENTRIES];
static struct tss tss_main, tss_df;
static u8 df_stack[DF_STACK_SIZE] __attribute__((aligned(16)));

void double_fault_task(void);   /* kernel/arch/interrupts.c */
struct tss *gdt_main_tss(void);

static void set_entry(int i, u32 base, u32 limit, u8 access, u8 flags)
{
    gdt[i].limit_lo = (u16)limit;
    gdt[i].base_lo = (u16)base;
    gdt[i].base_mid = (u8)(base >> 16);
    gdt[i].access = access;
    gdt[i].flags_limit_hi = (u8)(flags | ((limit >> 16) & 0x0F));
    gdt[i].base_hi = (u8)(base >> 24);
}

struct tss *gdt_main_tss(void) { return &tss_main; }

void gdt_init(void)
{
    set_entry(0, 0, 0, 0, 0);
    set_entry(1, 0, 0xFFFFF, ACC_CODE, FLAGS_4K_32);
    set_entry(2, 0, 0xFFFFF, ACC_DATA, FLAGS_4K_32);
    set_entry(3, (u32)&tss_main, sizeof(struct tss) - 1, ACC_TSS, 0);
    set_entry(4, (u32)&tss_df, sizeof(struct tss) - 1, ACC_TSS, 0);

    memset(&tss_main, 0, sizeof(tss_main));
    tss_main.iomap_base = sizeof(struct tss);

    memset(&tss_df, 0, sizeof(tss_df));
    tss_df.eip = (u32)double_fault_task;
    tss_df.esp = (u32)(df_stack + DF_STACK_SIZE);
    tss_df.cs = KERNEL_CS;
    tss_df.ds = tss_df.es = tss_df.fs = tss_df.gs = tss_df.ss = KERNEL_DS;
    tss_df.eflags = 0x2;                /* interrupts off, reserved bit 1 */
    tss_df.iomap_base = sizeof(struct tss);

    struct gdt_ptr p = { sizeof(gdt) - 1, (u32)gdt };
    __asm__ volatile(
        "lgdt %0\n\t"
        "ljmp %1, $1f\n"                /* reload CS */
        "1:\n\t"
        "mov %2, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %3, %%ax\n\t"
        "ltr %%ax"                      /* current task = tss_main */
        :: "m"(p), "i"(KERNEL_CS), "i"(KERNEL_DS), "i"(TSS_MAIN_SEL)
        : "eax", "memory");
    /* The CR3 field is only used on a task switch; paging is off, but keep
     * it valid anyway. */
    __asm__ volatile("mov %%cr3, %0" : "=r"(tss_df.cr3));
}
