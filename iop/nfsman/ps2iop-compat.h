/*
  The IOP side of unfs4's platform header, found through nfsman's include path. The IOP has no
  libc heap (nfsman.c supplies the allocator) and no read()/write() on sockets: nfsman.c maps
  those to its own bounded socket calls before it includes unfs4.c.
*/
#ifndef PS2IOP_COMPAT_H
#define PS2IOP_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <ps2ip.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);

#endif
