/* Basic fixed-width types for a freestanding 32-bit kernel (no libc). */
#ifndef P5_TYPES_H
#define P5_TYPES_H

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         s8;
typedef signed short        s16;
typedef signed int          s32;
typedef signed long long    s64;

typedef u32                 size_t;
typedef s32                 ssize_t;
typedef u32                 uintptr_t;
typedef _Bool               bool;

#define true    1
#define false   0
#define NULL    ((void *)0)

#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a)      (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a)    ((x) & ~((a) - 1))
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))

#define PACKED              __attribute__((packed))
#define NORETURN            __attribute__((noreturn))
#define PRINTF_FMT(f, a)    __attribute__((format(printf, f, a)))
#define STATIC_ASSERT(c, m) _Static_assert(c, m)

#endif
