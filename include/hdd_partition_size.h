#ifndef HDD_PARTITION_SIZE_H
#define HDD_PARTITION_SIZE_H

#include <stdint.h>

/* Creation offers 8 MiB, then multiples of 128 MiB. An existing volume's
 * original size also remains selectable when expanding it. Internal APA
 * extents can still be 8/16/32/64 MiB regardless of the size shown here. */
static inline int hddPartitionSizeStep(int current, int step, int minimum, int maximum)
{
    if (minimum < 8 || maximum < minimum) return -1;
    int top = maximum - maximum % 128;
    if (top < minimum) top = minimum;
    int64_t next = (int64_t)current + step;
    if (next <= minimum) return minimum;
    if (next >= top) return top;
    next -= next % 128;
    return next < minimum ? minimum : (int)next;
}

#endif
