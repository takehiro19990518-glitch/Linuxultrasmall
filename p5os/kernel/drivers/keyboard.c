/* PS/2 keyboard via the 8042 controller, IRQ 1, scancode set 1 (the 8042
 * translates the keyboard's set 2 by default).  US layout. */
#include <keyboard.h>
#include <arch.h>
#include <io.h>
#include <klog.h>

#define KBC_DATA        0x60
#define KBC_STATUS      0x64
#define KBC_CMD         0x64
#define ST_OUT_FULL     0x01
#define ST_IN_FULL      0x02
#define CMD_READ_CFG    0x20
#define CMD_WRITE_CFG   0x60
#define CFG_IRQ1        0x01
#define CFG_TRANSLATE   0x40
#define KBC_TIMEOUT     100000
#define FLUSH_LIMIT     32

#define SC_RELEASE      0x80
#define SC_EXTENDED     0xE0
#define SC_LSHIFT       0x2A
#define SC_RSHIFT       0x36
#define SC_CTRL         0x1D
#define SC_ALT          0x38
#define SC_CAPS         0x3A
#define SC_DELETE_KP    0x53

#define BUF_SIZE        64          /* power of two */

static const char map_normal[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
    /* 0x3B.. F1-F10, numlock, scrolllock, keypad */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
};
static const char map_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
};

static volatile int kbuf[BUF_SIZE];
static volatile u32 khead, ktail;
static u32 dropped;
static bool shift_l, shift_r, ctrl, alt, caps, extended;

static void push_key(int k)
{
    u32 next = (khead + 1) & (BUF_SIZE - 1);
    if (next == ktail) {
        dropped++;
        return;
    }
    kbuf[khead] = k;
    khead = next;
}

static int extended_key(u8 sc)
{
    switch (sc) {
    case 0x48: return KEY_UP;
    case 0x50: return KEY_DOWN;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x47: return KEY_HOME;
    case 0x4F: return KEY_END;
    case 0x53: return KEY_DELETE;
    case 0x49: return KEY_PGUP;
    case 0x51: return KEY_PGDN;
    case 0x52: return KEY_INSERT;
    case 0x1C: return '\n';         /* keypad enter */
    case 0x35: return '/';          /* keypad slash */
    default:   return 0;
    }
}

static void kbd_irq(struct int_frame *f)
{
    if (!(inb(KBC_STATUS) & ST_OUT_FULL))
        return;
    u8 sc = inb(KBC_DATA);

    if (sc == SC_EXTENDED) {
        extended = true;
        return;
    }
    bool release = sc & SC_RELEASE;
    u8 code = sc & (u8)~SC_RELEASE;
    bool ext = extended;
    extended = false;

    switch (code) {                 /* modifiers (left and right variants) */
    case SC_LSHIFT: if (!ext) shift_l = !release; return;
    case SC_RSHIFT: shift_r = !release; return;
    case SC_CTRL:   ctrl = !release; return;
    case SC_ALT:    alt = !release; return;
    default: break;
    }
    if (release)
        return;
    if (code == SC_CAPS) {
        caps = !caps;
        return;
    }
    if (ctrl && alt && code == SC_DELETE_KP) {
        push_key(KEY_CTRL_ALT_DEL);
        return;
    }
    if (ext) {
        int k = extended_key(code);
        if (k)
            push_key(k);
        return;
    }
    if (code >= 0x3B && code <= 0x44) {     /* F1-F10 */
        push_key(KEY_F1 + (code - 0x3B));
        return;
    }
    bool shift = shift_l || shift_r;
    char c = shift ? map_shift[code] : map_normal[code];
    if (!c)
        return;
    if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c ^= 0x20;
    if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c = (char)((c & 0x1F));             /* Ctrl+A = 1 ... Ctrl+Z = 26 */
    push_key((u8)c);
}

static bool kbc_wait_write(void)
{
    for (u32 i = 0; i < KBC_TIMEOUT; i++)
        if (!(inb(KBC_STATUS) & ST_IN_FULL))
            return true;
    return false;
}

static bool kbc_wait_read(void)
{
    for (u32 i = 0; i < KBC_TIMEOUT; i++)
        if (inb(KBC_STATUS) & ST_OUT_FULL)
            return true;
    return false;
}

void kbd_init(void)
{
    /* drop stale bytes left by the BIOS */
    for (int i = 0; i < FLUSH_LIMIT && (inb(KBC_STATUS) & ST_OUT_FULL); i++)
        (void)inb(KBC_DATA);

    /* make sure IRQ1 and set-1 translation are enabled in the 8042 config */
    if (kbc_wait_write()) {
        outb(KBC_CMD, CMD_READ_CFG);
        if (kbc_wait_read()) {
            u8 cfg = inb(KBC_DATA);
            u8 want = cfg | CFG_IRQ1 | CFG_TRANSLATE;
            if (want != cfg && kbc_wait_write()) {
                outb(KBC_CMD, CMD_WRITE_CFG);
                if (kbc_wait_write())
                    outb(KBC_DATA, want);
                LOG_INFO("kbd: 8042 config %02x -> %02x", cfg, want);
            }
        } else {
            LOG_WARN("kbd: 8042 did not answer config read");
        }
    } else {
        LOG_WARN("kbd: 8042 input buffer stuck full");
    }
    irq_register(IRQ_KEYBOARD, kbd_irq);
    pic_unmask(IRQ_KEYBOARD);
}

int kbd_trygetkey(void)
{
    u32 f = irq_save();
    int k = -1;
    if (ktail != khead) {
        k = kbuf[ktail];
        ktail = (ktail + 1) & (BUF_SIZE - 1);
    }
    irq_restore(f);
    return k;
}

int kbd_getkey(void)
{
    for (;;) {
        int k = kbd_trygetkey();
        if (k >= 0)
            return k;
        /* sti;hlt is atomic: an IRQ between the check and HLT still wakes us */
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

u32 kbd_dropped(void) { return dropped; }
