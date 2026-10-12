/* P5OS kernel main: bring up the hardware in a safe order, then the shell. */
#include <types.h>
#include <bootinfo.h>
#include <console.h>
#include <serial.h>
#include <printf.h>
#include <arch.h>
#include <timer.h>
#include <keyboard.h>
#include <klog.h>
#include <panic.h>
#include <shell.h>
#include <system.h>
#include <io.h>

void kmain(u32 magic, const struct bootinfo *bi);

static void banner(void)
{
    console_set_attr(ATTR_BRIGHT);
    kprintf("\n%s %s\n", P5OS_NAME, P5OS_VERSION);
    console_set_attr(ATTR_NORMAL);
    kprintf("CPU: %s\n", cpu_description());
    kprintf("Type 'help' for commands.\n\n");
}

void kmain(u32 magic, const struct bootinfo *bi)
{
    bool serial = serial_init();
    console_init(vga_text_init());
    console_set_attr(ATTR_OK);
    kprintf("P5OS Booted Successfully\n");
    console_set_attr(ATTR_NORMAL);

    if (magic != BOOTINFO_MAGIC)
        panic("bad boot magic %08x (expected %08x) - not started by P5OS stage 2?",
              magic, BOOTINFO_MAGIC);
    LOG_INFO("boot: drive %02x, kernel %u sectors at LBA %u, serial %s",
             bi->boot_drive, bi->kernel_sectors, bi->kernel_lba,
             serial ? "COM1" : "absent");

    gdt_init();
    idt_init();
    pic_init();
    stack_guard_init();
    timer_init();
    sti();
    LOG_INFO("cpu: GDT/IDT loaded, PIC remapped, PIT at %u Hz", TIMER_HZ);

    cpu_detect();
    LOG_INFO("cpu: %s", cpu_description());
    kbd_init();
    LOG_INFO("kbd: PS/2 keyboard ready");

    klog_set_console_level(KLOG_WARN);  /* quieter once booted */
    banner();
    shell_run();
}
