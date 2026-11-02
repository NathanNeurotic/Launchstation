/* Compile-only thread boundary for the production IOP module entry point. */
#ifndef FTP_TEST_STARTUP_H
#define FTP_TEST_STARTUP_H
#include "platform.h"
typedef struct {
    u32 attr;
    u32 option;
    void (*thread)(void *);
    u32 stacksize;
    u32 priority;
} iop_thread_t;
int CreateThread(iop_thread_t *thread);
int StartThread(int thread, void *argument);
#endif
