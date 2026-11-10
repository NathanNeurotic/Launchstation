#ifndef HOST_THSEMAP
#define HOST_THSEMAP
/* One thread on the host: the driver lock is a counter the harness checks is released. */
typedef struct
{
    int attr, option, initial, max;
} iop_sema_t;
extern int host_lock_depth;
static inline int CreateSema(iop_sema_t *s)
{
    (void)s;
    return 1;
}
static inline int WaitSema(int s)
{
    (void)s;
    return host_lock_depth++;
}
static inline int SignalSema(int s)
{
    (void)s;
    return --host_lock_depth;
}
static inline int DeleteSema(int s)
{
    (void)s;
    return 0;
}
#endif
