/*
  The IOP side of usmb2's platform header, found through smb2man's include path in place of
  upstream's ps2iop-compat.h (see usmb2/VENDORED.txt). The IOP is little-endian, has no libc heap
  (smb2man.c supplies the allocator) and no read()/write() on sockets: smb2man.c maps those to
  its own bounded socket calls before it includes usmb2.c.
*/
#ifndef PS2IOP_COMPAT_H
#define PS2IOP_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <ps2ip.h>

#ifndef O_DIRECTORY
#define O_DIRECTORY 0x10000 // usmb2_open() tests it; smb2man never calls usmb2_open()
#endif

#define htole16(x) (x)
#define le16toh(x) (x)
#define htole32(x) (x)
#define le32toh(x) (x)
#define htole64(x) (x)
#define le64toh(x) (x)

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);

#endif
