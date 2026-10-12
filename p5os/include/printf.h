#ifndef P5_PRINTF_H
#define P5_PRINTF_H
#include <types.h>
#include <stdarg.h>
/* Supported: %c %s %d %i %u %x %X %p %% with '-', '0', width, precision
 * (strings), 'l' (ignored) and 'll' (64-bit d/u/x). */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) PRINTF_FMT(3, 4);
int kprintf(const char *fmt, ...) PRINTF_FMT(1, 2);
/* 64-by-32 division without libgcc (__udivdi3 is not linked). */
u64 udiv64(u64 n, u32 d, u32 *rem);
#endif
