#ifndef P5_SYSTEM_H
#define P5_SYSTEM_H
#include <types.h>
#define P5OS_NAME       "P5OS"
#define P5OS_VERSION    "1.0"
NORETURN void system_reboot(void);
NORETURN void system_halt(void);
#endif
