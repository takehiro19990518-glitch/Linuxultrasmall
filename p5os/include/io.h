/* x86 port I/O and small CPU helpers (all available on the original Pentium). */
#ifndef P5_IO_H
#define P5_IO_H
#include <types.h>

static inline void outb(u16 port, u8 val) { __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port)); }
static inline void outw(u16 port, u16 val) { __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port)); }
static inline u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline u16 inw(u16 port) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static inline void insw(u16 port, void *buf, u32 count)
{
    __asm__ volatile("cld; rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}
static inline void outsw(u16 port, const void *buf, u32 count)
{
    __asm__ volatile("cld; rep outsw" : "+S"(buf), "+c"(count) : "d"(port) : "memory");
}

/* ~1us delay: write to an unused port (0x80, POST diagnostic port). */
static inline void io_wait(void) { outb(0x80, 0); }

static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void hlt(void) { __asm__ volatile("hlt" ::: "memory"); }

static inline u32 read_eflags(void)
{
    u32 f;
    __asm__ volatile("pushfl; popl %0" : "=r"(f));
    return f;
}
/* Save IF and disable interrupts; restore with irq_restore(). */
static inline u32 irq_save(void) { u32 f = read_eflags(); cli(); return f; }
static inline void irq_restore(u32 f) { if (f & 0x200) sti(); }

static inline u32 read_cr0(void) { u32 v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline u32 read_cr2(void) { u32 v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline u32 read_cr3(void) { u32 v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }

#endif
