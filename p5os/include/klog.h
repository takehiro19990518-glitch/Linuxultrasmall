/* Kernel log: ring buffer (shown by 'dmesg') + COM1 serial port. */
#ifndef P5_KLOG_H
#define P5_KLOG_H
#include <types.h>
enum { KLOG_DEBUG, KLOG_INFO, KLOG_WARN, KLOG_ERR };
void klog(int level, const char *fmt, ...) PRINTF_FMT(2, 3);
void klog_set_console_level(int level);     /* min level echoed on screen */
void klog_dump(void);                        /* print buffer to console */
void klog_tail(u32 lines);                   /* print the last N lines */
#define LOG_DBG(...)  klog(KLOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) klog(KLOG_INFO, __VA_ARGS__)
#define LOG_WARN(...) klog(KLOG_WARN, __VA_ARGS__)
#define LOG_ERR(...)  klog(KLOG_ERR, __VA_ARGS__)
#endif
