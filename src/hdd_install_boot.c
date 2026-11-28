#include "hdd_install.h"
#include <errno.h>
#include <delaythread.h>
#include <kernel.h>
#include <libcdvd.h>
#include <stdio.h>
#include <string.h>

enum { BOOT_OPEN_READ, BOOT_OPEN_WRITE, BOOT_READ, BOOT_WRITE, BOOT_CLOSE };
#define BOOT_FLAG_MASK 3u
#define BOOT_FLAG_ON 2u

/* Bounded MECHACON config access; no model-specific NVM addresses. */
static int bootCommand(int command, unsigned char *data)
{
    static const char *const names[] = {"open read", "open write", "read", "write", "close"};
    u32 status = 0xffffffff;
    int result = 0, attempt;
    for (attempt = 0; attempt < 300; attempt++) {
        status = 0xffffffff;
        switch (command) {
            case BOOT_OPEN_READ: case BOOT_OPEN_WRITE:
                result = sceCdOpenConfig(0, command == BOOT_OPEN_WRITE, 1, &status); break;
            case BOOT_READ: result = sceCdReadConfig(data, &status); break;
            case BOOT_WRITE: result = sceCdWriteConfig(data, &status); break;
            case BOOT_CLOSE: result = sceCdCloseConfig(&status); break;
        }
        if (result && !(status & 0x89)) return 0;
        if (status & 0x80) break; /* Config service unavailable/invalid request. */
        DelayThread(10000);
    }
    hddInstallLog("Console HDD boot: config %s failed result=%d status=0x%08x", names[command], result, (unsigned)status);
    return -EIO;
}

static int bootConfig(int writing, unsigned char *data)
{
    int ret = bootCommand(writing ? BOOT_OPEN_WRITE : BOOT_OPEN_READ, NULL), closed;
    if (ret < 0) return ret;
    ret = bootCommand(writing ? BOOT_WRITE : BOOT_READ, data);
    closed = bootCommand(BOOT_CLOSE, NULL);
    return ret < 0 ? ret : closed;
}

int hddReadBootFlag(void)
{
    unsigned char config[16] __attribute__((aligned(64))) = {0};
    int ret = bootConfig(0, config);
    return ret < 0 ? ret : (config[0] & BOOT_FLAG_MASK) == BOOT_FLAG_ON;
}

int hddEnableBootFlag(void)
{
    unsigned char before[16] __attribute__((aligned(64)));
    unsigned char desired[16] __attribute__((aligned(64)));
    unsigned char verify[16] __attribute__((aligned(64)));
    int ret;
    hddInstallStatus("Enabling console HDD boot", 0, 1);
    memset(before, 0, sizeof(before));
    ret = bootConfig(0, before);
    if (ret < 0) return ret;
    if ((before[0] & BOOT_FLAG_MASK) == BOOT_FLAG_ON) {
        hddInstallLog("Console HDD boot: already enabled; no NVM write");
        hddInstallStatus("Console HDD boot verified", 1, 1);
        return HDD_BOOT_ALREADY_ENABLED;
    }
    /* FMCB installer/system.c EnableHDDBooting: config set 0, one 15-byte
     * block. Low bits 2 enable ATAD support and HDD MBR boot. Preserve all
     * other bits/bytes; MECHACON owns the NVM layout and checksums. */
    memcpy(desired, before, sizeof(desired));
    desired[0] = (desired[0] & ~BOOT_FLAG_MASK) | BOOT_FLAG_ON;
    hddInstallLog("Console HDD boot: byte0 0x%02x -> 0x%02x", before[0], desired[0]);
    ret = bootConfig(1, desired);
    if (ret < 0) return ret;
    memset(verify, 0, sizeof(verify));
    ret = bootConfig(0, verify);
    if (ret < 0) return ret;
    if (memcmp(verify, desired, 15)) {
        hddInstallLog("Console HDD boot: readback mismatch (byte0=0x%02x)", verify[0]);
        return -EIO;
    }
    hddInstallLog("Console HDD boot: enabled and all 15 config bytes verified");
    hddInstallStatus("Console HDD boot verified", 1, 1);
    return HDD_BOOT_ENABLED;
}

int hddInstallEnableBoot(HddInstall *in)
{
    if (!(in->flags & HDD_INSTALL_HOSD)) return 0;
    int ret = hddEnableBootFlag();
    if (ret >= 0) return 0;
    snprintf(in->error, sizeof(in->error), "Software and MBR are installed, but enabling/verifying console HDD boot failed (error %d). See INSTALLER.LOG.", ret);
    hddInstallLog("ERROR: %s", in->error);
    return ret;
}
