/* Text console: cursor handling, scrolling, colours and serial mirroring
 * on top of a display backend (include/display.h). */
#ifndef P5_CONSOLE_H
#define P5_CONSOLE_H
#include <types.h>
#include <display.h>

#define ATTR_NORMAL     ATTR(COLOR_LGRAY, COLOR_BLACK)
#define ATTR_BRIGHT     ATTR(COLOR_WHITE, COLOR_BLACK)
#define ATTR_OK         ATTR(COLOR_LGREEN, COLOR_BLACK)
#define ATTR_WARN       ATTR(COLOR_YELLOW, COLOR_BLACK)
#define ATTR_ERROR      ATTR(COLOR_LRED, COLOR_BLACK)
#define ATTR_PANIC      ATTR(COLOR_WHITE, COLOR_RED)

void console_init(const struct display_ops *ops);
const struct display_ops *console_display(void);
void console_putc(char c);
void console_write(const char *s);
void console_clear(void);
void console_set_attr(u8 attr);
u8 console_get_attr(void);
void console_set_serial_mirror(bool on);
void console_fill_background(u8 attr);   /* recolour whole screen (panic) */
#endif
