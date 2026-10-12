#ifndef P5_STRING_H
#define P5_STRING_H
#include <types.h>
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
size_t strlcpy(char *d, const char *s, size_t size);   /* always NUL-terminates */
size_t strlcat(char *d, const char *s, size_t size);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
bool parse_u32(const char *s, u32 *out);               /* decimal or 0x hex */
#endif
