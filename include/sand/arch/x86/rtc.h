#ifndef SAND_ARCH_X86_RTC_H
#define SAND_ARCH_X86_RTC_H
/* CMOS real-time clock, wall time as set by the platform firmware (no time zone). */
int rtc_read_time(int *hour, int *minute, int *second);   /* 0 on success */
/* "YYYY-MM-DDTHH:MM:SS" from the CMOS clock, assumed to hold UTC. Not a trusted time source. 0 on success. */
int rtc_read_iso(char out[20]);
#endif
