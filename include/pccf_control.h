#ifndef LAUNCHSTATION_PCCF_CONTROL_H
#define LAUNCHSTATION_PCCF_CONTROL_H

/* Shared EE/IOP ABI. This endpoint controls only the owning CF module.
 * OFF is final for an IOP session, not a filesystem unmount or rescan. */
#define PCCF_CONTROL_DEVICE "pccfctl:"
#define PCCF_CONTROL_STATUS 0x50434601
#define PCCF_CONTROL_OFF    0x50434602
typedef struct {
    unsigned int version;
    unsigned int online;
    unsigned int sectors;
    unsigned int reserved;
} pccf_control_status_t;

#endif
