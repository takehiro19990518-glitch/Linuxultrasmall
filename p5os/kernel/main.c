/* P5OS kernel entry (Phase 1: boot check). */
#include <types.h>
#include <bootinfo.h>
#include <console.h>
#include <serial.h>
#include <printf.h>
#include <io.h>

void kmain(u32 magic, const struct bootinfo *bi);

void kmain(u32 magic, const struct bootinfo *bi)
{
    serial_init();
    console_init(vga_text_init());
    if (magic != BOOTINFO_MAGIC) {
        console_set_attr(ATTR_ERROR);
        kprintf("Bad boot magic %x\n", magic);
    } else {
        console_set_attr(ATTR_OK);
        kprintf("P5OS Booted Successfully\n");
        console_set_attr(ATTR_NORMAL);
        kprintf("boot drive %x, kernel %u sectors\n", bi->boot_drive, bi->kernel_sectors);
    }
    for (;;)
        hlt();
}
