#include <timer.h>
#include "launchelf.h"

// Timer Count, in ms since boot. clock() is a 32-bit microsecond count that
// wraps every 2^32 us (about 71.6 min), which made every Timer() deadline
// loop hang or end early after that much uptime; the 64-bit bus clock does
// not wrap.
u64 Timer(void)
{
	return GetTimerSystemTime() / (kBUSCLK / 1000);
}
