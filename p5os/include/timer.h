#ifndef P5_TIMER_H
#define P5_TIMER_H
#include <types.h>
#define TIMER_HZ 100
void timer_init(void);
u64 timer_ticks(void);
u64 timer_uptime_ms(void);
void timer_sleep_ms(u32 ms);
#endif
