#ifndef PCCF_TEST_LOADCORE_H
#define PCCF_TEST_LOADCORE_H
#define MODULE_RESIDENT_END 0
#define MODULE_NO_RESIDENT_END 1
typedef struct iop_library {
    struct iop_library *prev;
    void *caller;
    unsigned short version, flags;
    char name[8];
} iop_library_t;
typedef struct { iop_library_t *let_next, *let_prev; } lc_internals_t;
lc_internals_t *GetLoadcoreInternalData(void);
#endif
