#ifndef P5_SERIAL_H
#define P5_SERIAL_H
#include <types.h>
bool serial_init(void);             /* COM1, 38400 8N1; false if no UART */
bool serial_present(void);
void serial_putc(char c);
void serial_write(const char *s);
#endif
