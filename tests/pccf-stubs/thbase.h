int DelayThread(unsigned int microseconds);
#include "types.h"
typedef struct { u32 lo, hi; } iop_sys_clock_t;
int GetSystemTime(iop_sys_clock_t *clock);
int SysClock2USec(iop_sys_clock_t *clock, u32 *seconds, u32 *microseconds);
