/* Kernel panic: red diagnostic screen, register dump, back-trace, halt. */
#include <panic.h>
#include <arch.h>
#include <console.h>
#include <printf.h>
#include <serial.h>
#include <timer.h>
#include <klog.h>
#include <io.h>

#define MAX_TRACE   10

extern u8 __kernel_start[], __kernel_end[];
const char *exception_name(u32 vec);
static volatile int panicking;

static bool frame_ptr_ok(u32 ebp)
{
    return (ebp & 3) == 0 && ebp >= (u32)__kernel_start && ebp + 8 <= (u32)__kernel_end;
}

static void backtrace(u32 ebp, u32 eip)
{
    kprintf("Trace: %08x", eip);
    for (int i = 0; i < MAX_TRACE && ebp && frame_ptr_ok(ebp); i++) {
        u32 ret = ((u32 *)ebp)[1];
        if (!ret)
            break;
        kprintf(" %08x", ret);
        ebp = ((u32 *)ebp)[0];
    }
    kprintf("\n");
}

static NORETURN void do_panic(const struct int_frame *f, const char *fmt, va_list ap)
{
    char msg[160];
    cli();
    if (panicking++) {              /* panic inside panic: just stop */
        serial_write("\n*** recursive panic, halting ***\n");
        for (;;)
            hlt();
    }
    vsnprintf(msg, sizeof(msg), fmt, ap);
    console_set_serial_mirror(true);
    console_fill_background(ATTR_PANIC);
    kprintf("*** P5OS KERNEL PANIC ***\n\n%s\n", msg);
    if (f) {
        u32 err = f->err_code;
        kprintf("Exception %u: %s  error code %08x\n\n", f->int_no,
                exception_name(f->int_no), err);
        kprintf("EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n", f->eax, f->ebx, f->ecx, f->edx);
        kprintf("ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n", f->esi, f->edi, f->ebp,
                f->int_no == 8 ? f->esp_pusha : (u32)&f->eflags + 4);
        kprintf("EIP=%08x CS=%04x EFLAGS=%08x\n", f->eip, f->cs, f->eflags);
        kprintf("DS=%04x ES=%04x FS=%04x GS=%04x\n", f->ds, f->es, f->fs, f->gs);
        kprintf("CR0=%08x CR2=%08x CR3=%08x\n", read_cr0(), read_cr2(), read_cr3());
        backtrace(f->ebp, f->eip);
    } else {
        u32 ebp = (u32)__builtin_frame_address(0);
        backtrace(ebp, (u32)__builtin_return_address(0));
    }
    u32 rem;
    u64 secs = udiv64(timer_uptime_ms(), 1000, &rem);
    kprintf("Uptime %llu.%03u s, stack canary %s, peak stack %u bytes\n",
            secs, rem, stack_guard_ok() ? "intact" : "DAMAGED", stack_usage_peak());
    kprintf("\nRecent log:\n");
    klog_tail(4);
    kprintf("\nSystem halted. Press reset or power off the machine.\n");
    for (;;)
        hlt();
}

void panic(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    do_panic(NULL, fmt, ap);
}

void panic_frame(const struct int_frame *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    do_panic(f, fmt, ap);
}
