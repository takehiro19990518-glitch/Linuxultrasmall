/* Kernel log ring buffer.  Every message goes to the buffer and COM1;
 * messages at or above console_level are also shown on screen. */
#include <klog.h>
#include <console.h>
#include <serial.h>
#include <printf.h>
#include <timer.h>
#include <string.h>
#include <io.h>

#define KLOG_SIZE   8192
#define LINE_MAX    200

static char buf[KLOG_SIZE];
static u32 head;            /* next write position */
static bool wrapped;
static int console_level = KLOG_INFO;
static const char *const prefix[] = { "debug", "info", "WARN", "ERROR" };

void klog_set_console_level(int level) { console_level = level; }

static void ring_put(const char *s)
{
    for (; *s; s++) {
        buf[head++] = *s;
        if (head == KLOG_SIZE) {
            head = 0;
            wrapped = true;
        }
    }
}

void klog(int level, const char *fmt, ...)
{
    char line[LINE_MAX];
    u32 rem;
    u64 secs = udiv64(timer_uptime_ms(), 1000, &rem);
    int n = snprintf(line, sizeof(line), "[%5llu.%03u] %s: ", secs, rem,
                     prefix[level < 0 ? 0 : level > KLOG_ERR ? KLOG_ERR : level]);
    va_list ap;
    va_start(ap, fmt);
    if (n < (int)sizeof(line))
        vsnprintf(line + n, sizeof(line) - (u32)n, fmt, ap);
    va_end(ap);
    strlcat(line, "\n", sizeof(line));

    u32 f = irq_save();
    ring_put(line);
    irq_restore(f);

    if (level >= console_level) {
        u8 old = console_get_attr();
        if (level == KLOG_WARN)
            console_set_attr(ATTR_WARN);
        else if (level == KLOG_ERR)
            console_set_attr(ATTR_ERROR);
        console_write(line);            /* also mirrored to serial */
        console_set_attr(old);
    } else {
        serial_write(line);
    }
}

/* The ring viewed as a linear string, oldest byte first. */
static u32 ring_len(void) { return wrapped ? KLOG_SIZE : head; }
static char ring_at(u32 k) { return buf[((wrapped ? head : 0) + k) % KLOG_SIZE]; }

static void emit_from(u32 k)
{
    char tmp[2] = { 0, 0 };
    for (u32 n = ring_len(); k < n; k++) {
        tmp[0] = ring_at(k);
        console_write(tmp);
    }
}

void klog_dump(void)
{
    u32 k = 0;
    if (wrapped)                        /* skip the partially overwritten line */
        while (k < ring_len() && ring_at(k++) != '\n')
            ;
    emit_from(k);
}

void klog_tail(u32 lines)
{
    u32 n = ring_len(), count = 0, k;
    if (!n || !lines)
        return;
    for (k = n - 1; k > 0; k--)
        if (ring_at(k - 1) == '\n' && ++count == lines)
            break;
    emit_from(k);
}
