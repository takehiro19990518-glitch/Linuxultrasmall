/* 8259A programmable interrupt controller pair (master + cascaded slave). */
#include <arch.h>
#include <io.h>

#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1
#define ICW1_INIT   0x10
#define ICW1_ICW4   0x01
#define ICW4_8086   0x01
#define OCW2_EOI    0x20
#define OCW3_READ_ISR 0x0B

static u8 mask1 = 0xFF, mask2 = 0xFF;

void pic_init(void)
{
    /* ICW1: start initialisation, expect ICW4 (edge triggered, cascade) */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    /* ICW2: vector offsets, moving IRQs off the CPU exception vectors */
    outb(PIC1_DATA, IRQ_BASE); io_wait();
    outb(PIC2_DATA, IRQ_BASE + 8); io_wait();
    /* ICW3: slave on master IRQ2; slave cascade identity 2 */
    outb(PIC1_DATA, 1 << IRQ_CASCADE); io_wait();
    outb(PIC2_DATA, IRQ_CASCADE); io_wait();
    /* ICW4: 8086 mode, normal EOI */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    mask1 = (u8)~(1 << IRQ_CASCADE);    /* everything masked but cascade */
    mask2 = 0xFF;
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_unmask(u32 irq)
{
    if (irq < 8) {
        mask1 &= (u8)~(1 << irq);
        outb(PIC1_DATA, mask1);
    } else {
        mask2 &= (u8)~(1 << (irq - 8));
        outb(PIC2_DATA, mask2);
    }
}

void pic_mask(u32 irq)
{
    if (irq < 8) {
        mask1 |= (u8)(1 << irq);
        outb(PIC1_DATA, mask1);
    } else {
        mask2 |= (u8)(1 << (irq - 8));
        outb(PIC2_DATA, mask2);
    }
}

void pic_eoi(u32 irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, OCW2_EOI);
    outb(PIC1_CMD, OCW2_EOI);
}

/* In-service register of both PICs (used to detect spurious IRQ 7/15). */
u16 pic_read_isr(void)
{
    outb(PIC1_CMD, OCW3_READ_ISR);
    outb(PIC2_CMD, OCW3_READ_ISR);
    return (u16)(inb(PIC1_CMD) | (inb(PIC2_CMD) << 8));
}
