#include <assert.h>
#include <stdio.h>
#define _start cf_module_start
#include "../iop/pccf/main.c"
#undef _start
static lc_internals_t core;
static int missing_core, created, deleted, hw_calls, start_result, connected;
static iop_library_t lan, other;
static int wait_result, signals, stops, reads, writes;
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
int pccf_hw_present(void) { return 1; }
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
    assert(cf_module_start(0, NULL) == MODULE_RESIDENT_END);
    assert(created == 2 && deleted == 1 && hw_calls == 2 && connected == 1);
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
    puts("PASS: resident DEV9 rejection before MMIO, unavailable/cyclic registry fails closed, semaphore cleanup, CF-only BDM registration");
    return 0;
}
