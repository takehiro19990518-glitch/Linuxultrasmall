/* x86 descriptor tables, interrupts and CPU state. */
#ifndef P5_ARCH_H
#define P5_ARCH_H
#include <types.h>

#define KERNEL_CS       0x08
#define KERNEL_DS       0x10
#define TSS_MAIN_SEL    0x18
#define TSS_DF_SEL      0x20

#define IRQ_BASE        0x20        /* PIC master vectors 0x20-0x27 */
#define IRQ_COUNT       16
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_CASCADE     2

/* Register frame built by isr_common in kernel/arch/isr.asm. */
struct int_frame {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, esp_pusha, ebx, edx, ecx, eax;
    u32 int_no, err_code;
    u32 eip, cs, eflags;            /* pushed by the CPU (no ring change) */
};

typedef void (*irq_handler_t)(struct int_frame *f);

void gdt_init(void);
void gdt_set_df_cr3(u32 cr3);
void idt_init(void);
void irq_register(u32 irq, irq_handler_t h);
u32 irq_count(u32 irq);
u32 spurious_irq_count(void);

/* 8259 PIC */
void pic_init(void);
void pic_unmask(u32 irq);
void pic_mask(u32 irq);
void pic_eoi(u32 irq);
u16 pic_read_isr(void);

/* CPU identification (kernel/arch/cpu.c) */
struct cpu_info {
    bool has_cpuid;
    char vendor[13];
    u32 family, model, stepping;
    u32 features_edx;
    bool has_fpu, has_tsc, has_cx8, has_mmx, has_pse;
};
void cpu_detect(void);
const struct cpu_info *cpu_get(void);
const char *cpu_description(void);

/* Kernel stack (kernel/arch/entry.asm) */
extern u8 kernel_stack_bottom[];
extern u8 kernel_stack_top[];
void stack_guard_init(void);
bool stack_guard_ok(void);
u32 stack_usage_peak(void);

#endif
