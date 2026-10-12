/* VGA text mode driver (mode 3, 80x25, 16 colours) at physical 0xB8000. */
#include <display.h>
#include <io.h>

#define VGA_TEXT_BASE       ((volatile u16 *)0xB8000)
#define VGA_COLS            80
#define VGA_ROWS            25

/* CRT controller (colour adapter addresses) */
#define CRTC_INDEX          0x3D4
#define CRTC_DATA           0x3D5
#define CRTC_CURSOR_START   0x0A
#define CRTC_CURSOR_END     0x0B
#define CRTC_CURSOR_HI      0x0E
#define CRTC_CURSOR_LO      0x0F
#define CURSOR_DISABLE_BIT  0x20

static u8 cursor_start_saved = 0x0D;

static inline u16 cell(char c, u8 attr) { return (u16)(u8)c | ((u16)attr << 8); }

static void vga_putcell(u32 x, u32 y, char c, u8 attr)
{
    if (x < VGA_COLS && y < VGA_ROWS)
        VGA_TEXT_BASE[y * VGA_COLS + x] = cell(c, attr);
}

static void vga_scroll_up(u8 attr)
{
    volatile u16 *v = VGA_TEXT_BASE;
    for (u32 i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
        v[i] = v[i + VGA_COLS];
    for (u32 i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
        v[i] = cell(' ', attr);
}

static void vga_clear(u8 attr)
{
    for (u32 i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_TEXT_BASE[i] = cell(' ', attr);
}

/* Hardware cursor position is a linear cell index in CRTC regs 0x0E/0x0F. */
static void vga_set_cursor(u32 x, u32 y)
{
    u16 pos = (u16)(y * VGA_COLS + x);
    outb(CRTC_INDEX, CRTC_CURSOR_HI);
    outb(CRTC_DATA, (u8)(pos >> 8));
    outb(CRTC_INDEX, CRTC_CURSOR_LO);
    outb(CRTC_DATA, (u8)pos);
}

static void vga_show_cursor(bool on)
{
    outb(CRTC_INDEX, CRTC_CURSOR_START);
    outb(CRTC_DATA, on ? (u8)(cursor_start_saved & ~CURSOR_DISABLE_BIT)
                       : (u8)(cursor_start_saved | CURSOR_DISABLE_BIT));
}

static const struct display_ops vga_text_ops = {
    .name = "VGA text 80x25",
    .cols = VGA_COLS,
    .rows = VGA_ROWS,
    .putcell = vga_putcell,
    .scroll_up = vga_scroll_up,
    .clear = vga_clear,
    .set_cursor = vga_set_cursor,
    .show_cursor = vga_show_cursor,
};

const struct display_ops *vga_text_init(void)
{
    outb(CRTC_INDEX, CRTC_CURSOR_START);
    cursor_start_saved = inb(CRTC_DATA) & 0x1F;
    vga_show_cursor(true);
    return &vga_text_ops;
}
