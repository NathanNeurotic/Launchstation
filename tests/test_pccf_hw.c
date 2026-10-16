#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <dev9regs.h>
static unsigned short controls[16], saved_controls[16];
static unsigned int ssbus[3], saved_ssbus[3], write_count, byte_writes, delays, milliseconds;
static unsigned char cis[2048], taskfile[16], cor;
static int attach_fail, attach_calls;
static int delayed_cis, remove_on_wait, trace_bytes;
static int readback_fault;
static unsigned short r16(unsigned int address)
{
    unsigned int offset;
    if (address >= 0xb0000000U && address < 0xb0001000U) {
        offset = address - 0xb0000000U;
        assert(ssbus[0] & 0x1000U);
        if ((controls[0] & 3) == 3) return cis[offset / 2];
        assert((controls[0] & 3) == 0);
        switch (offset) {
            case 2: return 0x0101; case 4: return 0; case 6: return 0x50a0;
            case 0x0c: return 0x0100; case 0x0e: return 0x7e50;
            default: assert(0);
        }
    }
    assert(address >= DEV9_REGBASE && address <= DEV9_REGBASE + 30 && !(address & 1));
    return controls[(address - DEV9_REGBASE) / 2];
}
static void w16(unsigned int address, unsigned int value)
{
    assert(address >= DEV9_REGBASE && address <= DEV9_REGBASE + 30 && !(address & 1));
    assert(address != DEV9_REGBASE + DEV9_R_REV * 2 && address != DEV9_REGBASE + DEV9_R_1462 * 2);
    controls[(address - DEV9_REGBASE) / 2] = value; ++write_count;
}
static unsigned int ss_index(unsigned int address)
{
    assert(address == 0xbf801418U || address == 0xbf80141cU || address == 0xbf801420U);
    return (address - 0xbf801418U) / 4;
}
static unsigned int r32(unsigned int address) { return ssbus[ss_index(address)]; }
static void w32(unsigned int address, unsigned int value) { ssbus[ss_index(address)] = value; ++write_count; }
static unsigned char r8(unsigned int address)
{
    unsigned int offset = address - 0xb0000000U;
    assert(address >= 0xb0000000U && address < 0xb0001000U && !(ssbus[0] & 0x1000U));
    if ((controls[0] & 3) == 2) {
        assert(!(offset & 1));
        if (delayed_cis && milliseconds < 5022) return 0;
        if (offset >= 0x200 && offset <= 0x206) return offset == 0x200 ? cor : 0;
        return cis[offset / 2];
    }
    if ((controls[0] & 3) == 3) return cis[offset / 2];
    if (offset >= 2 && offset <= 6 && taskfile[2] >= 1 && taskfile[2] <= 64)
        return taskfile[offset] ^ ((readback_fault && offset == 3) ? 1 : 0);
    switch (offset) {
        case 7: case 0x0e: return 0x50; case 1: case 0x0d: case 2: case 3: return 1;
        case 4: case 5: return 0; case 6: return 0xa0; case 0x0f: return 0xfe;
        default: assert(0);
    }
}
static void w8(unsigned int address, unsigned int value)
{
    unsigned int offset = address - 0xb0000000U;
    assert(address >= 0xb0000000U && address < 0xb0000010U && !(ssbus[0] & 0x1000U) && !(controls[0] & 3));
    assert(offset == 2 || offset == 3 || offset == 4 || offset == 5 || offset == 6 || offset == 7);
    if (offset == 7) assert(value == 0xec || value == 0x20);
    taskfile[offset] = value; ++byte_writes;
}
#define PCCF_HW_TEST
#define HW_R16(a) r16(a)
#define HW_W16(a,v) w16(a,v)
#define HW_R32(a) r32(a)
#define HW_W32(a,v) w32(a,v)
#define HW_R8(a) r8(a)
#define HW_W8(a,v) w8(a,v)
#include "../iop/pccf/pccf_hw.c"
static const pcdiag_ata_ops_t *last_ops;
void pccardTrace(const char *format, ...)
{
    char line[512]; int count; va_list ap;
    assert(!strstr(format, "CIS DATA") && !strstr(format, "CF READ") && !strstr(format, "SD-DIAG ATTR"));
    va_start(ap, format); count = vsnprintf(line, sizeof(line), format, ap); va_end(ap);
    assert(count >= 0 && count < (int)sizeof(line)); trace_bytes += count;
}
int DelayThread(unsigned int us)
{
    assert(us == PCCF_SETTLE_US || us == 22000 || us == 500000 || us == 100000 || us == 1000000 || us == 4000000);
    ++delays; milliseconds += us / 1000;
    if (remove_on_wait && us == 1000000) controls[DEV9_R_1462] |= 3;
    return 0;
}
int GetSystemTime(iop_sys_clock_t *clock)
{
    clock->lo = milliseconds; clock->hi = 0; return 0;
}
int SysClock2USec(iop_sys_clock_t *clock, u32 *seconds, u32 *microseconds)
{
    *seconds = clock->lo / 1000; *microseconds = (clock->lo % 1000) * 1000; return 0;
}
int pccf_ata_attach(pccf_ata_t *ata, const pcdiag_ata_ops_t *ops, void *context, pcdiag_cis_trace_t trace)
{
    unsigned int before;
    (void)trace; ++attach_calls; last_ops = ops;
    ops->memory->set_access(context, 0, 8); before = byte_writes;
    assert(ops->issue_read_command(context, 0x30, 0) < 0);
    assert(ops->issue_read_command(context, 0xef, 0) < 0);
    assert(ops->issue_read_command(context, 0xec, 1) < 0);
    assert(ops->issue_read_command(context, 0x20, 0x10000000U) < 0 && byte_writes == before);
    assert(ops->issue_read_command(context, 0xec, 0) == 0 && byte_writes == before + 1);
    assert(ops->issue_read_command(context, 0x20, 0x0abcdef1U) == 0 && byte_writes == before + 7);
    assert(taskfile[6] == 0xea && taskfile[2] == 1 && taskfile[3] == 0xf1 && taskfile[4] == 0xde && taskfile[5] == 0xbc && taskfile[7] == 0x20);
    ops->memory->set_access(context, 3, 8); before = byte_writes;
    assert(ops->issue_read_command(context, 0xec, 0) < 0 && byte_writes == before);
    ops->memory->set_access(context, 0, 16);
    assert(ops->issue_read_command(context, 0x20, 0) < 0 && byte_writes == before);
    if (attach_fail) return -1;
    memset(ata, 0, sizeof(*ata));
    ata->ready = 1; ata->sectors = 7831152; return 0;
}
/* Unknown-model fixtures must keep conservative PIO without tune reads. */
int pccf_ata_read(pccf_ata_t *ata, unsigned int lba, unsigned int count, unsigned char *buffer)
{
    (void)ata; (void)lba; (void)count; (void)buffer;
    assert(!"unexpected profile read for unknown model"); return -1;
}
static void reset(void)
{
    unsigned int i; FILE *f;
    assert(!started);
    for (i = 0; i < 16; ++i) controls[i] = 0;
    controls[DEV9_R_REV] = 0x20; controls[DEV9_R_1462] = 0x108;
    controls[DEV9_R_1460] = 6; controls[DEV9_R_147E] = 1;
    controls[DEV9_R_1468] = 0x12; controls[DEV9_R_146A] = 0x55;
    ssbus[0] = 0x007a0099; ssbus[1] = 0x11223344; ssbus[2] = 0x87654321;
    memcpy(saved_controls, controls, sizeof(controls)); memcpy(saved_ssbus, ssbus, sizeof(ssbus));
    memset(cis, 0xff, sizeof(cis)); memset(taskfile, 0, sizeof(taskfile));
    f = fopen("tests/fixtures/transcend_ts4gcf133.cis", "rb"); assert(f);
    assert(fread(cis, 1, sizeof(cis), f) == 160); fclose(f);
    write_count = byte_writes = delays = milliseconds = cor = attach_calls = attach_fail = 0;
    delayed_cis = remove_on_wait = trace_bytes = readback_fault = 0;
}
static void restored(void)
{
    unsigned int i;
    assert(!started && controls[DEV9_R_POWER] == 0);
    for (i = 0; i < 7; ++i)
        assert(controls[(save16_addr[i] - 0x1460) / 2] == saved_controls[(save16_addr[i] - 0x1460) / 2]);
    assert(!memcmp(ssbus, saved_ssbus, sizeof(ssbus)));
}
int main(void)
{
    pccf_ata_t ata; unsigned int before;
    assert(REG(0x146e) == DEV9_REGBASE + DEV9_R_REV * 2);
    reset(); assert(pccf_hw_start(&ata) == 0 && ata.ready && attach_calls == 1);
    assert(controls[DEV9_R_POWER] == 0x17 && controls[DEV9_R_1474] == 0 && controls[DEV9_R_1460] == 4);
    assert(ssbus[0] == 0x1a00bb && delays == 7 && milliseconds == 1222 + 2 * (PCCF_SETTLE_US / 1000));
    before = byte_writes; readback_fault = 1;
    assert(last_ops->issue_read_command(NULL, 0x20, 0x800) < 0 && byte_writes == before + 5);
    readback_fault = 0;
    before = byte_writes;
    assert(ata.issue_read_sectors(NULL, 0x1000, 64) == 0 && byte_writes == before + 6 && taskfile[2] == 64);
    before = byte_writes;
    assert(ata.issue_read_sectors(NULL, 0x0fffffff, 2) < 0 && byte_writes == before);
    assert(ata.issue_read_sectors(NULL, 0, 0) < 0 && byte_writes == before);
    assert(ata.issue_read_sectors(NULL, 0, 65) < 0 && byte_writes == before);
    assert(pccf_hw_present()); controls[DEV9_R_1462] |= 3; before = byte_writes;
    assert(!pccf_hw_present() && last_ops->issue_read_command(NULL, 0xec, 0) < 0 && before == byte_writes);
    pccf_hw_stop(); restored();
    reset(); cor = 2; assert(pccf_hw_start(&ata) < 0 && !ata.ready && !attach_calls && !byte_writes); restored();
    reset(); attach_fail = 1; assert(pccf_hw_start(&ata) < 0 && attach_calls == 1); restored();
    reset(); controls[DEV9_R_REV] = 0x30; assert(pccf_hw_start(&ata) < 0 && !write_count && !byte_writes);
    reset(); controls[DEV9_R_1474] = 1;
    assert(pccf_hw_start(&ata) < 0 && !write_count && !attach_calls);
    reset(); controls[DEV9_R_REV] = 0xffff;
    assert(pccf_hw_start(&ata) < 0 && !write_count && !attach_calls);
    reset(); controls[DEV9_R_POWER] = 4; assert(pccf_hw_start(&ata) < 0 && !write_count); controls[DEV9_R_POWER] = 0;
    reset(); controls[DEV9_R_1462] = 0x10f; assert(pccf_hw_start(&ata) < 0 && !write_count);
    reset(); cis[0x33] = 0xfe; assert(pccf_hw_start(&ata) < 0 && !attach_calls && !byte_writes); restored();
    assert(trace_bytes < 2048 && milliseconds == 5522);
    reset(); memset(cis, 0, sizeof(cis));
    assert(pccf_hw_start(&ata) < 0 && !attach_calls && !byte_writes);
    assert(trace_bytes < 2048 && milliseconds == 5522); restored();
    reset(); delayed_cis = 1;
    assert(pccf_hw_start(&ata) == 0 && ata.ready && attach_calls == 1);
    assert(trace_bytes < 2048 && milliseconds == 6222 + 2 * (PCCF_SETTLE_US / 1000)); pccf_hw_stop(); restored();
    reset(); delayed_cis = remove_on_wait = 1;
    assert(pccf_hw_start(&ata) < 0 && !attach_calls && !byte_writes);
    assert(trace_bytes < 2048 && milliseconds == 1522); restored();
    reset(); controls[DEV9_R_1462] = 8; assert(pccf_hw_start(&ata) < 0 && !attach_calls); restored();
    reset(); controls[DEV9_R_1462] = 0x10c; assert(pccf_hw_start(&ata) == 0 && controls[DEV9_R_POWER] == 0x1b);
    pccf_hw_stop(); restored();
    puts("PASS: actual CF MMIO host power/reset/CIS, 3V/5V selection, persistent memory window, EC/20 whitelist/addressing, removal, failed initialization and full saved-register restoration");
    return 0;
}
