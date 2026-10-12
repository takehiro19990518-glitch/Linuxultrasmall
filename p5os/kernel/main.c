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
#include <mm.h>
#include <ata.h>
#include <fs.h>
#include <errno.h>

void kmain(u32 magic, const struct bootinfo *bi);

#define FDC_DOR             0x3F2   /* floppy digital output register */
#define FDC_DOR_MOTORS_OFF  0x0C    /* controller enabled, DMA on, all motors off */

/* The BIOS switches the floppy motor off from its IRQ 0 handler a few
 * seconds after the last access.  P5OS replaces that handler, so after a
 * floppy boot the motor would never stop on real hardware. */
static void floppy_motor_off(void)
{
    outb(FDC_DOR, FDC_DOR_MOTORS_OFF);
}

static void banner(void)
{
    console_set_attr(ATTR_BRIGHT);
    kprintf("\n%s %s\n", P5OS_NAME, P5OS_VERSION);
    console_set_attr(ATTR_NORMAL);
    kprintf("CPU: %s\n", cpu_description());
    kprintf("Memory: %u MB (%llu KB usable)\n", mem_installed_mb(), mem_usable_bytes() >> 10);
    kprintf("Storage: %s\n", fs_is_mounted() ? "P5FS mounted" : "no file system (storage commands unavailable)");
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

    memmap_init(bi);                    /* copy boot data before page 0 is unmapped */
    floppy_motor_off();
    gdt_init();
    idt_init();
    pic_init();
    stack_guard_init();
    LOG_INFO("cpu: GDT/IDT loaded, PIC remapped");

    pmm_init();
    paging_init();
    heap_init();

    timer_init();
    sti();
    LOG_INFO("timer: PIT channel 0 at %u Hz", TIMER_HZ);

    cpu_detect();
    LOG_INFO("cpu: %s", cpu_description());
    kbd_init();
    LOG_INFO("kbd: PS/2 keyboard ready");

    int ndisks = ata_init();
    LOG_INFO("ata: %d disk(s) found", ndisks);
    int rc = ndisks > 0 ? fs_mount_auto() : E_NODEV;
    if (rc)
        LOG_WARN("fs: no P5FS file system mounted (%s) - storage commands unavailable",
                 strerror(rc));

    klog_set_console_level(KLOG_WARN);  /* quieter once booted */
    banner();
    shell_run();
}
