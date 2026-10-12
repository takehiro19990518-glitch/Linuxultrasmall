/* System commands: help ver cls echo uptime reboot halt dmesg cpu crash */
#include <shell.h>
#include <console.h>
#include <printf.h>
#include <string.h>
#include <system.h>
#include <timer.h>
#include <klog.h>
#include <arch.h>
#include <keyboard.h>
#include <panic.h>

void shell_print_help(void);

static int cmd_help(int argc, char **argv, const char *rest)
{
    kprintf("Commands:\n");
    shell_print_help();
    kprintf("Keys: Up/Down history, Ctrl+C cancel line, Ctrl+L clear, Ctrl+Alt+Del reboot\n");
    return 0;
}

static int cmd_ver(int argc, char **argv, const char *rest)
{
    kprintf("%s %s (built %s %s)\n", P5OS_NAME, P5OS_VERSION, __DATE__, __TIME__);
    kprintf("Target: Intel Pentium (P5), 32-bit protected mode, 16 MB RAM\n");
    kprintf("CPU: %s\n", cpu_description());
    return 0;
}

static int cmd_cls(int argc, char **argv, const char *rest)
{
    console_clear();
    return 0;
}

static int cmd_echo(int argc, char **argv, const char *rest)
{
    kprintf("%s\n", rest);
    return 0;
}

static int cmd_uptime(int argc, char **argv, const char *rest)
{
    u32 ms;
    u64 total_s = udiv64(timer_uptime_ms(), 1000, &ms);
    u32 s = (u32)total_s;
    kprintf("Uptime: %u days, %02u:%02u:%02u.%03u (%llu ticks at %u Hz)\n",
            s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60, ms,
            timer_ticks(), TIMER_HZ);
    return 0;
}

static int cmd_reboot(int argc, char **argv, const char *rest)
{
    kprintf("Rebooting...\n");
    timer_sleep_ms(100);            /* let the serial FIFO drain */
    system_reboot();
}

static int cmd_halt(int argc, char **argv, const char *rest)
{
    system_halt();
}

static int cmd_dmesg(int argc, char **argv, const char *rest)
{
    klog_dump();
    return 0;
}

static int cmd_cpu(int argc, char **argv, const char *rest)
{
    const struct cpu_info *c = cpu_get();
    kprintf("CPU: %s\n", cpu_description());
    if (c->has_cpuid)
        kprintf("Features: FPU=%s TSC=%s CX8=%s PSE=%s MMX=%s (edx=%08x)\n",
                c->has_fpu ? "yes" : "no", c->has_tsc ? "yes" : "no",
                c->has_cx8 ? "yes" : "no", c->has_pse ? "yes" : "no",
                c->has_mmx ? "yes" : "no", c->features_edx);
    kprintf("IRQ counts: timer=%u keyboard=%u spurious=%u, keys dropped=%u\n",
            irq_count(IRQ_TIMER), irq_count(IRQ_KEYBOARD), spurious_irq_count(),
            kbd_dropped());
    return 0;
}

/* Deliberately fault, to test the exception/panic path. */
static int cmd_crash(int argc, char **argv, const char *rest)
{
    if (argc < 2) {
        kprintf("usage: crash div0|gpf|ud|stack|panic   (halts the system!)\n");
        return 1;
    }
    if (!strcmp(argv[1], "div0")) {
        u32 q, zero = 0;          /* explicit divl: GCC folds 1/x to x==1 */
        __asm__ volatile("divl %2" : "=a"(q) : "a"(1), "r"(zero), "d"(0));
        kprintf("%u\n", q);
    } else if (!strcmp(argv[1], "gpf")) {
        __asm__ volatile("mov %0, %%ds" :: "r"(0x1234));
    } else if (!strcmp(argv[1], "ud")) {
        __asm__ volatile(".byte 0xFF, 0xFF");      /* FF /7: undefined on all x86 */
    } else if (!strcmp(argv[1], "stack")) {
        /* recurse until the stack runs out */
        static int (*volatile self)(int, char **, const char *) = cmd_crash;
        volatile char pad[512];
        pad[0] = 0;
        self(argc, argv, rest + pad[0]);
    } else if (!strcmp(argv[1], "panic")) {
        panic("test panic requested from shell");
    }
    kprintf("crash: unknown kind '%s'\n", argv[1]);
    return 1;
}

const struct shell_cmd sys_commands[] = {
    { "help",   "",         "list commands", cmd_help },
    { "ver",    "",         "show OS version", cmd_ver },
    { "cls",    "",         "clear the screen", cmd_cls },
    { "echo",   "<text>",   "print text", cmd_echo },
    { "uptime", "",         "time since boot", cmd_uptime },
    { "reboot", "",         "restart the computer", cmd_reboot },
    { "halt",   "",         "stop the system", cmd_halt },
    { "dmesg",  "",         "show the kernel log", cmd_dmesg },
    { "cpu",    "",         "CPU and interrupt information", cmd_cpu },
    { "crash",  "<kind>",   "test exception handling (halts)", cmd_crash },
    { NULL, NULL, NULL, NULL },
};
