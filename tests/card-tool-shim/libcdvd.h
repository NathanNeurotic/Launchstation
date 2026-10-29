/* Host stand-in for libcdvd.h (tests only): the RTC read used for card timestamps. */
#ifndef LIBCDVD_H
#define LIBCDVD_H
typedef struct {
	unsigned char stat, second, minute, hour, pad, day, month, year;
} sceCdCLOCK;
int sceCdReadClock(sceCdCLOCK *clock);
#endif
