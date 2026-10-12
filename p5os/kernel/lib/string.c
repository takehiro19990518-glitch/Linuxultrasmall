/* Freestanding string/memory routines.  GCC may emit calls to memcpy and
 * memset itself (e.g. for struct copies), so these must always exist. */
#include <string.h>

void *memset(void *d, int c, size_t n)
{
    u8 *p = d;
    while (n--)
        *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    void *ret = d;
    /* rep movsd + rep movsb: fast on the P5 and no alignment faults */
    u32 dw = n >> 2, b = n & 3;
    __asm__ volatile("cld; rep movsl; mov %3, %%ecx; rep movsb"
                     : "+D"(d), "+S"(s), "+c"(dw) : "r"(b) : "memory");
    return ret;
}

void *memmove(void *d, const void *s, size_t n)
{
    u8 *dp = d;
    const u8 *sp = s;
    if (dp == sp || n == 0)
        return d;
    if (dp < sp || dp >= sp + n)
        return memcpy(d, s, n);
    while (n--)
        dp[n] = sp[n];
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (u8)*a - (u8)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

size_t strlcpy(char *d, const char *s, size_t size)
{
    size_t len = strlen(s);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return len;
}

size_t strlcat(char *d, const char *s, size_t size)
{
    size_t dl = strlen(d);
    if (dl >= size)
        return dl + strlen(s);
    return dl + strlcpy(d + dl, s, size - dl);
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (!*s)
            return (char *)last;
    }
}

bool parse_u32(const char *s, u32 *out)
{
    u32 v = 0, base = 10;
    if (!s || !*s)
        return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (!*s)
            return false;
    }
    for (; *s; s++) {
        u32 d;
        if (*s >= '0' && *s <= '9')
            d = (u32)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = (u32)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = (u32)(*s - 'A' + 10);
        else
            return false;
        if (v > (0xFFFFFFFFu - d) / base)
            return false;               /* overflow */
        v = v * base + d;
    }
    *out = v;
    return true;
}
