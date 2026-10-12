#ifndef P5_RTC_H
#define P5_RTC_H
#include <types.h>
struct rtc_time {
    u32 year, month, day, hour, minute, second;
};
bool rtc_read(struct rtc_time *t);      /* false if the RTC looks invalid */
u32 rtc_unix_time(void);                /* 0 if unavailable */
void unix_to_rtc(u32 t, struct rtc_time *out);
#endif
