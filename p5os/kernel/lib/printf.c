/* Small printf implementation for the kernel. */
#include <printf.h>
#include <string.h>
#include <console.h>

#define KPRINTF_BUF 512

u64 udiv64(u64 n, u32 d, u32 *rem)
{
    u32 hi = (u32)(n >> 32), lo = (u32)n;
    u32 qhi = hi / d, r = hi % d, qlo;
    /* r < d, so the 64/32 divl cannot overflow */
    __asm__("divl %4" : "=a"(qlo), "=d"(r) : "a"(lo), "d"(r), "rm"(d));
    if (rem)
        *rem = r;
    return ((u64)qhi << 32) | qlo;
}

struct outbuf {
    char *buf;
    size_t size, len;
};

static void emit(struct outbuf *o, char c)
{
    if (o->len + 1 < o->size)
        o->buf[o->len] = c;
    o->len++;
}

static void emit_num(struct outbuf *o, u64 v, u32 base, bool upper, bool neg,
                     int width, bool zero, bool left)
{
    char tmp[24];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        u32 r;
        v = udiv64(v, base, &r);
        tmp[n++] = digits[r];
    } while (v);
    int len = n + (neg ? 1 : 0);
    if (!left && !zero)
        for (; width > len; width--)
            emit(o, ' ');
    if (neg)
        emit(o, '-');
    if (!left && zero)
        for (; width > len; width--)
            emit(o, '0');
    while (n)
        emit(o, tmp[--n]);
    if (left)
        for (; width > len; width--)
            emit(o, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct outbuf o = { buf, size, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            emit(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false, zero = false;
        int width = 0, prec = -1, lng = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = true;
            else if (*fmt == '0') zero = true;
            else break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            prec = 0;
            fmt++;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }
        switch (*fmt) {
        case 'c':
            emit(&o, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = (int)strlen(s);
            if (prec >= 0 && len > prec)
                len = prec;
            if (!left)
                for (int w = width; w > len; w--)
                    emit(&o, ' ');
            for (int i = 0; i < len; i++)
                emit(&o, s[i]);
            if (left)
                for (int w = width; w > len; w--)
                    emit(&o, ' ');
            break;
        }
        case 'd':
        case 'i': {
            s64 v = lng >= 2 ? va_arg(ap, s64) : va_arg(ap, s32);
            bool neg = v < 0;
            emit_num(&o, neg ? (u64)(-v) : (u64)v, 10, false, neg, width, zero, left);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            u64 v = lng >= 2 ? va_arg(ap, u64) : va_arg(ap, u32);
            emit_num(&o, v, *fmt == 'u' ? 10 : 16, *fmt == 'X', false, width, zero, left);
            break;
        }
        case 'p':
            emit(&o, '0');
            emit(&o, 'x');
            emit_num(&o, (uintptr_t)va_arg(ap, void *), 16, false, false, 8, true, false);
            break;
        case '%':
            emit(&o, '%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            emit(&o, '%');
            emit(&o, *fmt);
            break;
        }
    }
    if (size)
        buf[o.len < size ? o.len : size - 1] = '\0';
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int kprintf(const char *fmt, ...)
{
    char buf[KPRINTF_BUF];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_write(buf);
    return n;
}
