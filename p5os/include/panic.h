#ifndef P5_PANIC_H
#define P5_PANIC_H
#include <types.h>
struct int_frame;
NORETURN void panic(const char *fmt, ...) PRINTF_FMT(1, 2);
NORETURN void panic_frame(const struct int_frame *f, const char *fmt, ...) PRINTF_FMT(2, 3);
#define ASSERT(c) do { if (!(c)) panic("assertion failed: %s (%s:%d)", #c, __FILE__, __LINE__); } while (0)
#endif
