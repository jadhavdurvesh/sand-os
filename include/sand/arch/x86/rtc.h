#ifndef SAND_ARCH_X86_RTC_H
#define SAND_ARCH_X86_RTC_H
/* CMOS real-time clock, wall time as set by the platform firmware (no time zone). */
int rtc_read_time(int *hour, int *minute, int *second);   /* 0 on success */
#endif
