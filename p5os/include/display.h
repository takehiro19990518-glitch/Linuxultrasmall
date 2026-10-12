/* Display backend interface.  The console (kernel/console.c) only talks to
 * the screen through this table, so a VGA graphics-mode backend can be
 * added later without touching the console or the shell. */
#ifndef P5_DISPLAY_H
#define P5_DISPLAY_H
#include <types.h>

struct display_ops {
    const char *name;
    u32 cols, rows;
    void (*putcell)(u32 x, u32 y, char c, u8 attr);
    void (*scroll_up)(u8 attr);             /* move all rows up by one */
    void (*clear)(u8 attr);
    void (*set_cursor)(u32 x, u32 y);
    void (*show_cursor)(bool on);
};

/* VGA text mode (80x25) backend, drivers/vga_text.c */
const struct display_ops *vga_text_init(void);

/* Colour attributes (VGA text palette) */
enum {
    COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN, COLOR_RED,
    COLOR_MAGENTA, COLOR_BROWN, COLOR_LGRAY, COLOR_DGRAY, COLOR_LBLUE,
    COLOR_LGREEN, COLOR_LCYAN, COLOR_LRED, COLOR_LMAGENTA, COLOR_YELLOW,
    COLOR_WHITE
};
#define ATTR(fg, bg)    ((u8)(((bg) << 4) | (fg)))

#endif
