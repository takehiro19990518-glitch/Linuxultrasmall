/* MC146818-compatible CMOS real-time clock (read only).  The RTC is
 * assumed to hold UTC (QEMU's default); no time zone handling. */
#include <rtc.h>
#include <io.h>

#define CMOS_INDEX      0x70
#define CMOS_DATA       0x71
#define NMI_DISABLE     0x80
#define REG_SEC         0x00
#define REG_MIN         0x02
#define REG_HOUR        0x04
#define REG_DAY         0x07
#define REG_MONTH       0x08
#define REG_YEAR        0x09
#define REG_STATUS_A    0x0A
#define REG_STATUS_B    0x0B
#define REG_CENTURY     0x32    /* not on every chipset; IBM PC/AT convention */
#define STA_UIP         0x80    /* update in progress */
#define STB_24H         0x02
#define STB_BINARY      0x04
#define HOUR_PM         0x80
#define UIP_SPIN        100000

static u8 cmos(u8 reg)
{
    outb(CMOS_INDEX, reg);      /* keep NMI enabled (bit 7 clear) */
    return inb(CMOS_DATA);
}

static u32 bcd(u8 v) { return (u32)((v & 0x0F) + (v >> 4) * 10); }

static void read_raw(u8 r[7])
{
    for (u32 i = 0; i < UIP_SPIN && (cmos(REG_STATUS_A) & STA_UIP); i++)
        ;
    r[0] = cmos(REG_SEC);
    r[1] = cmos(REG_MIN);
    r[2] = cmos(REG_HOUR);
    r[3] = cmos(REG_DAY);
    r[4] = cmos(REG_MONTH);
    r[5] = cmos(REG_YEAR);
    r[6] = cmos(REG_CENTURY);
}

bool rtc_read(struct rtc_time *t)
{
    u8 a[7], b[7];
    u32 f = irq_save();
    /* read until two consecutive reads agree (avoids update tearing) */
    read_raw(a);
    for (int tries = 0; tries < 5; tries++) {
        read_raw(b);
        bool same = true;
        for (int i = 0; i < 7; i++)
            same &= a[i] == b[i];
        if (same)
            break;
        for (int i = 0; i < 7; i++)
            a[i] = b[i];
    }
    u8 sb = cmos(REG_STATUS_B);
    irq_restore(f);

    bool pm = a[2] & HOUR_PM;
    a[2] &= (u8)~HOUR_PM;
    if (!(sb & STB_BINARY)) {
        t->second = bcd(a[0]); t->minute = bcd(a[1]); t->hour = bcd(a[2]);
        t->day = bcd(a[3]); t->month = bcd(a[4]); t->year = bcd(a[5]);
        a[6] = (u8)bcd(a[6]);
    } else {
        t->second = a[0]; t->minute = a[1]; t->hour = a[2];
        t->day = a[3]; t->month = a[4]; t->year = a[5];
    }
    if (!(sb & STB_24H)) {
        if (t->hour == 12)
            t->hour = 0;
        if (pm)
            t->hour += 12;
    }
    u32 century = (a[6] >= 19 && a[6] <= 21) ? a[6] : (t->year < 80 ? 20 : 19);
    t->year += century * 100;
    return t->month >= 1 && t->month <= 12 && t->day >= 1 && t->day <= 31 &&
           t->hour < 24 && t->minute < 60 && t->second < 60;
}

static bool leap(u32 y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static const u8 mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

u32 rtc_unix_time(void)
{
    struct rtc_time t;
    if (!rtc_read(&t) || t.year < 1970 || t.year > 2105)
        return 0;
    u32 days = 0;
    for (u32 y = 1970; y < t.year; y++)
        days += leap(y) ? 366 : 365;
    for (u32 m = 1; m < t.month; m++)
        days += mdays[m - 1] + (m == 2 && leap(t.year) ? 1 : 0);
    days += t.day - 1;
    return days * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

void unix_to_rtc(u32 ts, struct rtc_time *o)
{
    u32 days = ts / 86400, rem = ts % 86400;
    o->hour = rem / 3600;
    o->minute = (rem / 60) % 60;
    o->second = rem % 60;
    u32 y = 1970;
    while (days >= (leap(y) ? 366u : 365u)) {
        days -= leap(y) ? 366 : 365;
        y++;
    }
    u32 m = 0;
    while (m < 12) {
        u32 len = mdays[m] + (m == 1 && leap(y) ? 1 : 0);
        if (days < len)
            break;
        days -= len;
        m++;
    }
    o->year = y;
    o->month = m + 1;
    o->day = days + 1;
}
