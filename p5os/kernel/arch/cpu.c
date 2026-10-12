/* CPU identification via CPUID (present on every Pentium). */
#include <arch.h>
#include <string.h>
#include <printf.h>

#define EFLAGS_ID       (1u << 21)
#define FEAT_FPU        (1u << 0)
#define FEAT_PSE        (1u << 3)
#define FEAT_TSC        (1u << 4)
#define FEAT_CX8        (1u << 8)
#define FEAT_MMX        (1u << 23)

static struct cpu_info info;
static char desc[80];

static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}

static bool cpuid_supported(void)
{
    u32 before, after;
    __asm__ volatile(
        "pushfl\n\tpopl %0\n\t"
        "movl %0, %1\n\t"
        "xorl %2, %1\n\t"
        "pushl %1\n\tpopfl\n\t"
        "pushfl\n\tpopl %1\n\t"
        "pushl %0\n\tpopfl"
        : "=&r"(before), "=&r"(after) : "i"(EFLAGS_ID));
    return ((before ^ after) & EFLAGS_ID) != 0;
}

void cpu_detect(void)
{
    memset(&info, 0, sizeof(info));
    strlcpy(info.vendor, "unknown", sizeof(info.vendor));
    info.has_cpuid = cpuid_supported();
    if (info.has_cpuid) {
        u32 a, b, c, d, max;
        cpuid(0, &max, &b, &c, &d);
        memcpy(info.vendor, &b, 4);
        memcpy(info.vendor + 4, &d, 4);
        memcpy(info.vendor + 8, &c, 4);
        info.vendor[12] = '\0';
        if (max >= 1) {
            cpuid(1, &a, &b, &c, &d);
            info.stepping = a & 0xF;
            info.model = (a >> 4) & 0xF;
            info.family = (a >> 8) & 0xF;
            info.features_edx = d;
            info.has_fpu = d & FEAT_FPU;
            info.has_tsc = d & FEAT_TSC;
            info.has_cx8 = d & FEAT_CX8;
            info.has_mmx = d & FEAT_MMX;
            info.has_pse = d & FEAT_PSE;
        }
    }

    const char *name = "x86 compatible";
    if (!strcmp(info.vendor, "GenuineIntel") && info.family == 5) {
        /* model 1 = P5 (60/66 MHz), 2/3/7 = P54C, 4/8 = P55C (MMX) */
        if (info.model == 1)
            name = "Intel Pentium (P5, 60/66 MHz class)";
        else if (info.has_mmx)
            name = "Intel Pentium MMX";
        else
            name = "Intel Pentium";
    } else if (!strcmp(info.vendor, "GenuineIntel") && info.family == 4) {
        name = "Intel 486 class";
    } else if (!strcmp(info.vendor, "GenuineIntel") && info.family >= 6) {
        name = "Intel P6 or later (Pentium compatible)";
    } else if (!info.has_cpuid) {
        name = "386/486 class (no CPUID)";
    }
    if (info.has_cpuid)
        snprintf(desc, sizeof(desc), "%s [%s fam %u model %u step %u]", name,
                 info.vendor, info.family, info.model, info.stepping);
    else
        snprintf(desc, sizeof(desc), "%s", name);
}

const struct cpu_info *cpu_get(void) { return &info; }
const char *cpu_description(void) { return desc; }
