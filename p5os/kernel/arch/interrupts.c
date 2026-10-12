/* IDT setup, interrupt dispatch, CPU exception handling. */
#include <arch.h>
#include <io.h>
#include <panic.h>
#include <klog.h>
#include <string.h>
#include <mm.h>

#define IDT_ENTRIES         256
#define EXC_COUNT           32
#define VEC_DOUBLE_FAULT    8
#define VEC_NMI             2
#define VEC_BREAKPOINT      3
#define VEC_PAGE_FAULT      14
#define PF_PRESENT          0x1
#define PF_WRITE            0x2
#define GATE_INT32          0x8E    /* present, ring 0, 32-bit interrupt gate */
#define GATE_TASK           0x85    /* present, ring 0, task gate */
#define SYS_CTRL_B          0x61    /* NMI source bits 6 (IOCHK) / 7 (parity) */

struct idt_entry {
    u16 off_lo;
    u16 sel;
    u8  zero;
    u8  type;
    u16 off_hi;
} PACKED;

struct idt_ptr {
    u16 limit;
    u32 base;
} PACKED;

extern const u32 isr_stub_table[48];
static struct idt_entry idt[IDT_ENTRIES];
static irq_handler_t irq_handlers[IRQ_COUNT];
static u32 irq_counts[IRQ_COUNT];
static u32 spurious;

static const char *const exc_names[EXC_COUNT] = {
    "#DE Divide error", "#DB Debug", "NMI", "#BP Breakpoint",
    "#OF Overflow", "#BR BOUND range exceeded", "#UD Invalid opcode",
    "#NM Device not available", "#DF Double fault", "Coprocessor segment overrun",
    "#TS Invalid TSS", "#NP Segment not present", "#SS Stack-segment fault",
    "#GP General protection fault", "#PF Page fault", "Reserved (15)",
    "#MF x87 floating-point error", "#AC Alignment check", "#MC Machine check",
    "#XM SIMD exception", "Reserved (20)", "Reserved (21)", "Reserved (22)",
    "Reserved (23)", "Reserved (24)", "Reserved (25)", "Reserved (26)",
    "Reserved (27)", "Reserved (28)", "Reserved (29)", "Reserved (30)",
    "Reserved (31)",
};

const char *exception_name(u32 vec);
const char *exception_name(u32 vec)
{
    return vec < EXC_COUNT ? exc_names[vec] : "Unknown interrupt";
}

static void set_gate(u32 vec, u32 handler, u16 sel, u8 type)
{
    idt[vec].off_lo = (u16)handler;
    idt[vec].sel = sel;
    idt[vec].zero = 0;
    idt[vec].type = type;
    idt[vec].off_hi = (u16)(handler >> 16);
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));        /* unused vectors -> #NP -> panic */
    for (u32 i = 0; i < 48; i++)
        set_gate(i, isr_stub_table[i], KERNEL_CS, GATE_INT32);
    /* #DF runs as a separate task with its own stack (see gdt.c) */
    set_gate(VEC_DOUBLE_FAULT, 0, TSS_DF_SEL, GATE_TASK);

    struct idt_ptr p = { sizeof(idt) - 1, (u32)idt };
    __asm__ volatile("lidt %0" :: "m"(p));

    /* stage 2 disabled NMI in the CMOS index port; turn it back on */
    outb(0x70, inb(0x70) & 0x7F);
}

void irq_register(u32 irq, irq_handler_t h)
{
    if (irq < IRQ_COUNT)
        irq_handlers[irq] = h;
}

u32 irq_count(u32 irq) { return irq < IRQ_COUNT ? irq_counts[irq] : 0; }
u32 spurious_irq_count(void) { return spurious; }

static void handle_irq(struct int_frame *f)
{
    u32 irq = f->int_no - IRQ_BASE;

    /* Spurious IRQ 7/15: the ISR bit is not set.  No EOI for IRQ 7; for
     * IRQ 15 the master still needs one (for the cascade line). */
    if (irq == 7 || irq == 15) {
        if (!(pic_read_isr() & (1u << irq))) {
            spurious++;
            if (irq == 15)
                pic_eoi(0);
            return;
        }
    }
    irq_counts[irq]++;
    if (irq_handlers[irq])
        irq_handlers[irq](f);
    pic_eoi(irq);
}

void interrupt_dispatch(struct int_frame *f);
void interrupt_dispatch(struct int_frame *f)
{
    if (f->int_no >= IRQ_BASE && f->int_no < IRQ_BASE + IRQ_COUNT) {
        handle_irq(f);
        return;
    }
    if (f->int_no == VEC_BREAKPOINT) {          /* int3: log and continue */
        LOG_WARN("breakpoint at eip=%08x", f->eip);
        return;
    }
    if (f->int_no == VEC_NMI) {
        u8 b = inb(SYS_CTRL_B);
        panic_frame(f, "NMI: %s", (b & 0x80) ? "memory parity error" :
                    (b & 0x40) ? "I/O channel check" : "unknown source");
    }
    if (f->int_no == VEC_PAGE_FAULT) {
        u32 addr = read_cr2();
        const char *why = addr < PAGE_SIZE ? "NULL pointer dereference" :
                          paging_is_guard(addr) ? "guard page hit (stack overflow?)" :
                          !paging_is_mapped(addr) ? "address not mapped (beyond RAM?)" :
                          "protection violation";
        panic_frame(f, "page fault: %s %08x - %s", (f->err_code & PF_WRITE) ? "write to" :
                    "read from", addr, why);
    }
    panic_frame(f, "unhandled CPU exception");
}

/* Entered via the #DF task gate.  The faulting context was saved by the
 * CPU into the main TSS during the task switch. */
struct tss_regs {
    u32 prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs;
};
struct tss *gdt_main_tss(void);
void double_fault_task(void);
void double_fault_task(void)
{
    const struct tss_regs *t = (const struct tss_regs *)gdt_main_tss();
    struct int_frame f;
    memset(&f, 0, sizeof(f));
    f.int_no = VEC_DOUBLE_FAULT;
    f.eax = t->eax; f.ebx = t->ebx; f.ecx = t->ecx; f.edx = t->edx;
    f.esi = t->esi; f.edi = t->edi; f.ebp = t->ebp; f.esp_pusha = t->esp;
    f.eip = t->eip; f.cs = t->cs; f.eflags = t->eflags;
    f.ds = t->ds; f.es = t->es; f.fs = t->fs; f.gs = t->gs;
    /* A fault while pushing onto the guard page below the stack means the
     * kernel stack overflowed. */
    bool overflow = paging_is_guard(read_cr2()) || paging_is_guard(t->esp) ||
                    paging_is_guard(t->esp - 4);
    panic_frame(&f, "double fault%s", overflow ? ": KERNEL STACK OVERFLOW (guard page hit)" : "");
}

/* ---- kernel stack canary ------------------------------------------------- */
#define STACK_CANARY        0x5AFEC0DEu
#define STACK_CANARY_WORDS  4
#define STACK_FILL          0xA5A5A5A5u

void stack_guard_init(void)
{
    /* Fill the unused part of the stack so the peak depth can be measured. */
    u32 *p = (u32 *)kernel_stack_bottom;
    u32 here = (u32)__builtin_frame_address(0) - 256;
    for (; (u32)p < here; p++)
        *p = STACK_FILL;
    for (u32 i = 0; i < STACK_CANARY_WORDS; i++)
        ((u32 *)kernel_stack_bottom)[i] = STACK_CANARY;
}

bool stack_guard_ok(void)
{
    const volatile u32 *p = (const volatile u32 *)kernel_stack_bottom;
    for (u32 i = 0; i < STACK_CANARY_WORDS; i++)
        if (p[i] != STACK_CANARY)
            return false;
    return true;
}

u32 stack_usage_peak(void)
{
    const u32 *p = (const u32 *)kernel_stack_bottom + STACK_CANARY_WORDS;
    while ((const u8 *)p < kernel_stack_top && *p == STACK_FILL)
        p++;
    return (u32)(kernel_stack_top - (const u8 *)p);
}
