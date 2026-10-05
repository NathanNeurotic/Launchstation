#ifndef MMCE_CONTROL_H
#define MMCE_CONTROL_H

#include <stddef.h>

int mmceControlProbe(int unit);
int mmceControlGetStatus(int unit);
int mmceControlGetCard(int unit);
int mmceControlGetChannel(int unit);
int mmceControlGetGameId(int unit, char *out, size_t out_size);
int mmceControlWaitIdle(int unit, int attempts, int delay_ms);

/* If firmware is currently presenting BootCard (card index 0), request its
 * native UNMOUNT_BOOTCARD transition and wait for the resulting switch.
 * Returns 1 if a switch was requested, 0 if no BootCard was active, <0 on
 * protocol/timeout failure. */
int mmceControlRestoreFromBootCard(int unit);

#endif
