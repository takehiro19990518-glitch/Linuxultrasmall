/* Reboot and halt. */
#include <system.h>
#include <io.h>
#include <console.h>
#include <printf.h>
#include <klog.h>

#define KBC_STATUS      0x64
#define KBC_CMD         0x64
#define KBC_PULSE_RESET 0xFE
#define KBC_TIMEOUT     100000
#define RESET_WAIT_MS   2000

void system_reboot(void)
{
    LOG_INFO("system: rebooting");
    cli();
    /* 1) pulse the CPU reset line through the 8042 keyboard controller */
    for (u32 i = 0; i < KBC_TIMEOUT && (inb(KBC_STATUS) & 0x02); i++)
        ;
    outb(KBC_CMD, KBC_PULSE_RESET);
    for (u32 i = 0; i < RESET_WAIT_MS * 1000; i++)
        io_wait();                  /* ~1 us each */

    /* 2) fallback: triple fault via an empty IDT */
    struct PACKED { u16 limit; u32 base; } null_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int $3" :: "m"(null_idt));
    for (;;)
        hlt();
}

void system_halt(void)
{
    LOG_INFO("system: halted");
    console_set_attr(ATTR_BRIGHT);
    kprintf("\nSystem halted. It is now safe to turn off the computer.\n");
    cli();
    for (;;)
        hlt();                      /* NMI may wake us: loop */
}
