/* 8253/8254 PIT channel 0 as the system tick (IRQ 0). */
#include <timer.h>
#include <arch.h>
#include <io.h>
#include <printf.h>
#include <panic.h>

#define PIT_CH0         0x40
#define PIT_CMD         0x43
#define PIT_INPUT_HZ    1193182u
#define PIT_MODE_RATE   0x34    /* channel 0, lo/hi byte, mode 2, binary */
#define PIT_DIVISOR     ((PIT_INPUT_HZ + TIMER_HZ / 2) / TIMER_HZ)

static volatile u64 ticks;

static void timer_irq(struct int_frame *f)
{
    ticks++;
    /* No guard page without paging: check the stack canary instead. */
    if (!stack_guard_ok())
        panic_frame(f, "kernel stack overflow (canary overwritten)");
}

void timer_init(void)
{
    outb(PIT_CMD, PIT_MODE_RATE);
    outb(PIT_CH0, (u8)(PIT_DIVISOR & 0xFF));
    outb(PIT_CH0, (u8)(PIT_DIVISOR >> 8));
    irq_register(IRQ_TIMER, timer_irq);
    pic_unmask(IRQ_TIMER);
}

u64 timer_ticks(void)
{
    u32 f = irq_save();         /* 64-bit read is not atomic on i386 */
    u64 t = ticks;
    irq_restore(f);
    return t;
}

/* Exact conversion using the real tick period (divisor / 1193182 s). */
u64 timer_uptime_ms(void)
{
    return udiv64(timer_ticks() * PIT_DIVISOR * 1000u, PIT_INPUT_HZ, NULL);
}

void timer_sleep_ms(u32 ms)
{
    u64 end = timer_uptime_ms() + ms;
    while (timer_uptime_ms() < end)
        hlt();                  /* woken by the next timer tick */
}
