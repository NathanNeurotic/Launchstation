#include <assert.h>
#include <stdio.h>
#define _start cf_module_start
#include "../iop/pccf/main.c"
#undef _start
static lc_internals_t core;
static int missing_core, created, deleted, hw_calls, start_result, connected;
static iop_library_t lan, other;
static int wait_result, signals, stops, reads, writes;
static int add_result, added, present = 1;
int iomanX_AddDrv(iomanX_iop_device_t *dev) {
    assert(!strcmp(dev->name, "pccfctl")); ++added; return add_result;
}
lc_internals_t *GetLoadcoreInternalData(void) { return missing_core ? NULL : &core; }
int CreateSema(iop_sema_t *s) { assert(s->initial == 1 && s->max == 1); ++created; return 3; }
int DeleteSema(int s) { assert(s == 3); ++deleted; return 0; }
int WaitSema(int s) { assert(s == 3); return wait_result; }
int SignalSema(int s) { assert(s == 3); ++signals; return 0; }
void bdm_connect_bd(struct block_device *b) {
    assert(b->sectorSize == 512 && b->sectorCount == 1000);
    assert(!strcmp(b->name, "cf") && !strcmp(b->path, "cf")); ++connected;
}
int pccf_hw_start(pccf_ata_t *a) { ++hw_calls; a->sectors = 1000; a->ready = !start_result; return start_result; }
int pccf_hw_present(void) { return present; }
void pccf_hw_stop(void) { ++stops; }
int pccf_ata_read(pccf_ata_t *a, unsigned int l, unsigned int n, unsigned char *b) { (void)a;(void)l;(void)n;(void)b;++reads;return 0; }
int pccf_ata_write(pccf_ata_t *a, unsigned int l, unsigned int n, const unsigned char *b) { (void)a;(void)l;(void)n;(void)b;++writes;return 0; }
int main(void) {
    memcpy(lan.name, "dev9", 5); memcpy(other.name, "bdm", 4);
    other.prev = &lan; core.let_next = &other;
    assert(cf_module_start(0, NULL) == MODULE_NO_RESIDENT_END);
    assert(!created && !hw_calls && !connected);
    core.let_next = NULL; missing_core = 1;
    assert(cf_module_start(0, NULL) == MODULE_NO_RESIDENT_END && !created);
    missing_core = 0; other.prev = &other; core.let_next = &other;
    assert(cf_module_start(0, NULL) == MODULE_NO_RESIDENT_END && !created);
    core.let_next = NULL; start_result = -1;
    assert(cf_module_start(0, NULL) == MODULE_NO_RESIDENT_END);
    assert(created == 1 && deleted == 1 && hw_calls == 1 && !connected);
    start_result = 0;
    add_result = -1;
    assert(cf_module_start(0, NULL) == MODULE_NO_RESIDENT_END);
    assert(created == 2 && deleted == 2 && added == 1 && !connected && stops == 1);
    add_result = 0; stops = 0;
    assert(cf_module_start(0, NULL) == MODULE_RESIDENT_END);
    assert(created == 3 && deleted == 2 && hw_calls == 3 && connected == 1);
    {
        unsigned char sector[512];
        wait_result = -1;
        assert(cf_read(&device, 0, sector, 1) == -EIO);
        assert(cf_write(&device, 0, sector, 1) == -EIO);
        assert(cf_stop(&device) == -EIO);
        assert(!reads && !writes && !stops && !signals && ata.ready);
        wait_result = 0;
        assert(cf_read(&device, 0, sector, 1) == 1);
        assert(cf_write(&device, 0, sector, 1) == 1);
        assert(cf_stop(&device) == 0);
        assert(reads == 1 && writes == 1 && stops == 1 && signals == 3 && !ata.ready);
    }
    {
        iomanX_iop_file_t file = {0};
        pccf_control_status_t status;
        assert(control_deinit(&control_device) == -EBUSY);
        assert(control_devctl(&file, "", PCCF_CONTROL_STATUS, NULL, 0, &status, sizeof(status)) == 0);
        assert(status.version == 1 && !status.online && status.sectors == 1000 && !status.reserved);
        assert(control_devctl(&file, "", PCCF_CONTROL_STATUS, NULL, 0, &status, sizeof(status)-1) == -EINVAL);
        assert(control_devctl(&file, "", PCCF_CONTROL_OFF, &status, 0, NULL, 0) == -EINVAL);
        file.unit = 1;
        assert(control_devctl(&file, "", PCCF_CONTROL_OFF, NULL, 0, NULL, 0) == -ENODEV);
        file.unit = 0; wait_result = -1;
        assert(control_devctl(&file, "", PCCF_CONTROL_STATUS, NULL, 0, &status, sizeof(status)) == -EIO);
        assert(control_devctl(&file, "", PCCF_CONTROL_OFF, NULL, 0, NULL, 0) == -EIO);
        wait_result = 0; ata.ready = 1; present = 0;
        assert(control_devctl(&file, "", PCCF_CONTROL_STATUS, NULL, 0, &status, sizeof(status)) == 0);
        assert(!status.online && !ata.ready);
        present = 1;
        assert(control_devctl(&file, "", PCCF_CONTROL_STATUS, NULL, 0, &status, sizeof(status)) == 0);
        assert(!status.online); /* Reinsertion never revives old BDM pointers. */
        assert(control_devctl(&file, "", PCCF_CONTROL_OFF, NULL, 0, NULL, 0) == 0);
        assert(control_devctl(&file, "", PCCF_CONTROL_OFF, NULL, 0, NULL, 0) == 0);
    }
    puts("PASS: resident DEV9 rejection before MMIO, unavailable/cyclic registry fails closed, semaphore cleanup, CF-only BDM registration");
    return 0;
}
