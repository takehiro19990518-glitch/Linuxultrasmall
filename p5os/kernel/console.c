/* Console: terminal semantics (\n, \r, \b, \t, scrolling) over a display
 * backend, with every character optionally mirrored to the serial port. */
#include <console.h>
#include <serial.h>

#define TAB_WIDTH 8

static const struct display_ops *disp;
static u32 cx, cy;
static u8 cur_attr = ATTR_NORMAL;
static bool mirror = true;

void console_init(const struct display_ops *ops)
{
    disp = ops;
    cx = cy = 0;
    disp->clear(cur_attr);
    disp->set_cursor(0, 0);
}

const struct display_ops *console_display(void) { return disp; }
void console_set_attr(u8 attr) { cur_attr = attr; }
u8 console_get_attr(void) { return cur_attr; }
void console_set_serial_mirror(bool on) { mirror = on; }

static void newline(void)
{
    cx = 0;
    if (++cy >= disp->rows) {
        disp->scroll_up(cur_attr);
        cy = disp->rows - 1;
    }
}

static void putc_raw(char c)
{
    switch (c) {
    case '\n':
        newline();
        break;
    case '\r':
        cx = 0;
        break;
    case '\b':                      /* move left, wrapping to previous line */
        if (cx > 0)
            cx--;
        else if (cy > 0) {
            cy--;
            cx = disp->cols - 1;
        }
        break;
    case '\t':
        do {
            putc_raw(' ');
        } while (cx % TAB_WIDTH);
        break;
    default:
        disp->putcell(cx, cy, c, cur_attr);
        if (++cx >= disp->cols)
            newline();
        break;
    }
}

void console_putc(char c)
{
    if (mirror)
        serial_putc(c);
    if (!disp)
        return;
    putc_raw(c);
    disp->set_cursor(cx, cy);
}

void console_write(const char *s)
{
    if (mirror)
        serial_write(s);
    if (!disp)
        return;
    while (*s)
        putc_raw(*s++);
    disp->set_cursor(cx, cy);
}

void console_clear(void)
{
    if (!disp)
        return;
    disp->clear(cur_attr);
    cx = cy = 0;
    disp->set_cursor(0, 0);
}

void console_fill_background(u8 attr)
{
    cur_attr = attr;
    console_clear();
}
