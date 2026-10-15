#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../iop/pccf/pcdiag_cf.h"

typedef struct {
    unsigned int space, width, reads8, reads16, changes, waits, milliseconds;
    unsigned char cor;
} mock_bus_t;
static char output[16384];
static unsigned int used;
static unsigned char cis[PCDIAG_CIS_BUS_LIMIT / 2];
static void capture(const char *format, ...)
{
    va_list ap;
    int size;
    va_start(ap, format);
    size = vsnprintf(output + used, sizeof(output) - used, format, ap);
    va_end(ap);
    assert(size >= 0 && (unsigned int)size < sizeof(output) - used - 1);
    used += size;
    output[used++] = '\n';
    output[used] = 0;
}
static unsigned char read_cis(void *context, unsigned int offset)
{
    assert(context == cis && offset < PCDIAG_CIS_BUS_LIMIT && !(offset & 1));
    return cis[offset / 2];
}
static unsigned char read8(void *context, unsigned int offset)
{
    mock_bus_t *bus = context;
    assert(bus->width == 8);
    ++bus->reads8;
    if (bus->space == 2) {
        assert(offset >= 0x200 && offset <= 0x206 && !(offset & 1));
        return offset == 0x200 ? bus->cor : (unsigned char)(offset & 0xff);
    }
    assert(bus->space == 0 || bus->space == 3);
    assert((offset >= 1 && offset <= 7) || (offset >= 0x0d && offset <= 0x0f));
    /* In this mock, selector 3 is unconnected; 0 is common memory. */
    if (bus->space == 3) return 0xff;
    if (offset == 7 || offset == 0x0e) return 0x50;
    if (offset == 1 || offset == 0x0d || offset == 2 || offset == 3) return 1;
    return 0;
}
static unsigned short read16(void *context, unsigned int offset)
{
    mock_bus_t *bus = context;
    assert(bus->width == 16 && (bus->space == 0 || bus->space == 3));
    assert(offset == 2 || offset == 4 || offset == 6 || offset == 0x0c || offset == 0x0e);
    ++bus->reads16;
    if (bus->space == 3) return 0xffff;
    if (offset == 2) return 0x0101;
    if (offset == 6) return 0x5000;
    if (offset == 0x0c) return 0x0100;
    if (offset == 0x0e) return 0x0050;
    return 0;
}
static void set_access(void *context, unsigned int space, unsigned int width)
{
    mock_bus_t *bus = context;
    assert(space == 0 || space == 3);
    assert(width == 8 || width == 16);
    /* Twelve changes: 0/8, 0/16, 3/8, 3/16, three repetitions. */
    assert(space == ((bus->changes % 4) < 2 ? 0U : 3U));
    assert(width == ((bus->changes % 2) == 0 ? 8U : 16U));
    bus->space = space;
    bus->width = width;
    ++bus->changes;
}
static void wait_ms(void *context, unsigned int milliseconds)
{
    mock_bus_t *bus = context;
    assert(milliseconds == (bus->waits == 0 ? 500U : 100U));
    assert(bus->waits < 3);
    ++bus->waits;
    bus->milliseconds += milliseconds;
}
static const pcdiag_cf_ops_t ops = {read8,read16,set_access,wait_ms};
static void reset(mock_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->space = 2; bus->width = 8;
    used = 0; output[0] = 0;
}
static void assert_profile_rejected(pcdiag_cis_info_t info)
{
    mock_bus_t bus;
    reset(&bus);
    assert(!pcdiag_cis_cf_candidate(&info));
    assert(pcdiag_cf_probe(&info, &ops, &bus, capture) == 1);
    assert(bus.reads8 == 0 && bus.reads16 == 0 && bus.changes == 0 && bus.waits == 0);
}
int main(void)
{
    pcdiag_cis_info_t info, changed;
    mock_bus_t bus;
    FILE *fixture;
    unsigned int i;
    size_t count;
    memset(cis, 0xff, sizeof(cis));
    fixture = fopen("tests/fixtures/transcend_ts4gcf133.cis", "rb");
    assert(fixture);
    count = fread(cis, 1, sizeof(cis), fixture);
    assert(!ferror(fixture) && fclose(fixture) == 0 && count == 160);
    used = 0;
    assert(pcdiag_cis_dump_info(read_cis, cis, capture, &info) == 0);
    assert(info.complete && info.function == 4 && info.config_base == 0x200);
    assert(info.register_mask == 0x0f && pcdiag_cis_cf_candidate(&info));
    assert(strstr(output, "TRANSCEND|TS4GCF133||"));

    reset(&bus);
    assert(pcdiag_cf_probe(&info, &ops, &bus, capture) == 0);
    assert(bus.reads8 == 64 && bus.reads16 == 30 && bus.changes == 12);
    assert(bus.waits == 3 && bus.milliseconds == 700);
    assert(strstr(output, "SPACE=0 ST8=50 ALT8=50 ST16_hi=50 ALT16_lo=50"));
    assert(strstr(output, "SPACE=3 ST8=ff ALT8=ff ST16_hi=ff ALT16_lo=ff"));
    assert(strstr(output, "CF PROBE END"));
    assert(used < 5000);

    /* COR with a selected I/O index, reset asserted or floating bus: skip
       without any selector changes, delays, wide reads, or card writes. */
    for (i = 0; i < 4; ++i) {
        static const unsigned char bad_cor[] = {1,2,0x80,0xff};
        reset(&bus); bus.cor = bad_cor[i];
        assert(pcdiag_cf_probe(&info, &ops, &bus, capture) == 1);
        assert(bus.reads8 == 4 && bus.reads16 == 0 && bus.changes == 0 && bus.waits == 0);
    }
    /* Never read configuration registers that were not declared present. */
    changed = info; changed.register_mask = 1;
    reset(&bus); bus.cor = 0x40; /* LevIREQ may be set with memory index 0. */
    assert(pcdiag_cf_probe(&changed, &ops, &bus, capture) == 0);
    assert(bus.reads8 == 61);

    changed=info; changed.complete=0; assert_profile_rejected(changed);
    changed=info; changed.malformed=1; assert_profile_rejected(changed);
    changed=info; changed.linked=1; assert_profile_rejected(changed);
    changed=info; changed.function_count=2; assert_profile_rejected(changed);
    changed=info; changed.function=6; assert_profile_rejected(changed); /* LAN PC CARD */
    changed=info; changed.function=0xfe; assert_profile_rejected(changed);
    changed=info; changed.config_count=2; assert_profile_rejected(changed);
    changed=info; changed.config_base=0x400; assert_profile_rejected(changed);
    changed=info; changed.register_mask=0; assert_profile_rejected(changed);
    changed=info; changed.memory_index_zero=0; assert_profile_rejected(changed);

    /* Truncate the real fixture before END; no partially decoded profile
       can qualify for hardware access. */
    cis[159] = 0;
    used = 0;
    memset(cis+160, 0, sizeof(cis)-160);
    assert(pcdiag_cis_dump_info(read_cis, cis, capture, &changed) == -2);
    assert_profile_rejected(changed);
    puts("PASS: actual CF CIS profile, rejected profiles/COR, exact non-data addresses, selector/width sequence and bounded probe duration");
    return 0;
}
