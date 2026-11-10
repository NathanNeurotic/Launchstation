#include <stddef.h>
#define ALLOC_FIRST 0
// run.sh renames malloc/free with -D; asm labels bind the real symbols (and keep ASan's hooks).
extern void *host_malloc(size_t) __asm__("malloc");
extern void host_free(void *) __asm__("free");
static inline void *AllocSysMemory(int m, int size, void *a)
{
    (void)m;
    (void)a;
    size_t *p = host_malloc(size + 16);
    if (!p)
        return NULL;
    p[0] = size;
    return (char *)p + 16;
}
static inline int FreeSysMemory(void *p)
{
    host_free((char *)p - 16);
    return 0;
}
static inline int QueryBlockSize(void *p) { return (int)((size_t *)((char *)p - 16))[0]; }
static inline unsigned int QueryTotalFreeMemSize(void) { return 2u * 1024 * 1024; }
static inline unsigned int QueryMaxFreeMemSize(void) { return 1024u * 1024; }
