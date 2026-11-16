#ifndef MC_UNIT_H
#define MC_UNIT_H

#include <stddef.h>

/* Memory card unit N of "mcN:" (0-7). MCMAN reads it as port N & 1, multitap
 * slot N >> 1, so mc0/mc1 are slot A of the two ports and mc2-mc7 sit behind
 * a multitap. libmc calls take that port and slot, never N itself. */
#define MC_UNITS 8
#define MC_UNIT_PORT(unit) ((unit) & 1)
#define MC_UNIT_SLOT(unit) ((unit) >> 1)
/* N for a path on "mcN:", else -1. */
static inline int mcPathUnit(const char *path)
{
	return (path != NULL && path[0] == 'm' && path[1] == 'c' && path[2] >= '0' &&
	        path[2] < '0' + MC_UNITS && path[3] == ':') ?
	           path[2] - '0' :
	           -1;
}

#endif
