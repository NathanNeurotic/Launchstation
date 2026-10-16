#include "pccf_ata_card.h"
static pccf_ata_t ata;
static int issue_many(void *context, unsigned int lba, unsigned int count)
{
    int result;
    assert(count && count <= 64 && count <= 12288 - lba);
    result = issue(context, 0x20, lba);
    card.count = count;
    return result;
}
static int attach(void)
{
    static const pcdiag_cf_ops_t memory = {read8, read16, set_access, wait_ms};
    static const pcdiag_ata_ops_t ops = {&memory, issue};
    return pccf_ata_attach(&ata, &ops, &card, trace);
}
int main(void)
{
    unsigned int f, before, i; unsigned char data[512];
    reset(); card.delay = 1;
    assert(attach() == 0 && ata.ready && ata.sectors == 12288 && card.words == 256);
    assert(!strcmp(ata.model, "TRANSCEND TS4GCF133") && !strcmp(ata.serial, "SERIAL123"));
    before = card.commands;
    assert(pccf_ata_read(&ata, 12288, 1, data) < 0 && pccf_ata_read(&ata, 12287, 2, data) < 0);
    assert(pccf_ata_read(&ata, 0, 0, data) < 0 && pccf_ata_read(&ata, 0, 65, data) < 0 && card.commands == before);
    assert(pccf_ata_read(&ata, 0, 1, data) == 0 && card.words == 512 && ata.sector_reads == 1);
    for (i = 0; i < 10; ++i) assert(pccf_ata_read(&ata, 0, 1, data) == 0); /* No DIAG5 six-read limit. */
    reset(); card.fault = BAD_MAP;
    assert(attach() < 0 && card.commands == 0 && !ata.ready);
    reset(); card.zero_signature = 1; make_fat(0);
    assert(attach() == 0 && ata.ready && card.commands == 1 && card.words == 256);
    assert(strstr(log_buffer, "zero startup signature accepted"));
    assert(pccf_ata_read(&ata, 0, 1, data) == 0 && card.reads == 1);
    reset(); card.zero_signature = 1; card.fault = BAD_MAP;
    assert(attach() < 0 && !ata.ready && card.commands == 0);
    reset(); card.zero_signature = 1; card.bad_signature = 1;
    assert(attach() < 0 && !ata.ready && card.commands == 0);
    reset(); card.zero_signature = 1; card.initial_status = 0x80;
    assert(attach() < 0 && !ata.ready && card.commands == 0);
    assert(card.waits == 5000);
    reset(); card.initial_busy_ms = 1500;
    assert(attach() == 0 && ata.ready && card.commands == 1 && card.waits == 1500);
    reset(); card.zero_signature = 1; card.initial_busy_ms = 4500;
    assert(attach() == 0 && ata.ready && card.commands == 1 && card.waits == 4500);
    reset(); card.zero_signature = 1; card.initial_status = 0x51;
    assert(attach() < 0 && !ata.ready && card.commands == 0);
    reset(); card.zero_signature = 1; memset(card.id, 0, sizeof(card.id));
    assert(attach() < 0 && !ata.ready && card.commands == 1 && card.reads == 0);
    for (f = REJECT_COMMAND; f <= FINAL_ERROR; ++f) {
        reset(); card.zero_signature = 1; card.fault = f;
        assert(attach() < 0 && !ata.ready && card.commands == 1 && card.reads == 0);
    }
    for (f = REJECT_COMMAND; f <= FINAL_ERROR; ++f) {
        reset(); card.fault = f; assert(attach() < 0 && !ata.ready && card.commands == 1);
        assert(card.words == (f >= COMPLETE_TIMEOUT ? 256U : 0U));
        if (f == DRQ_TIMEOUT || f == COMPLETE_TIMEOUT) assert(card.waits == 2000);
    }
    reset(); make_fat(0); assert(attach() == 0);
    card.fault = DRQ_ERROR; card.fault_command = 2;
    assert(pccf_ata_read(&ata, 0, 1, data) < 0 && !ata.ready);
    before = card.commands;
    assert(pccf_ata_read(&ata, 0, 1, data) < 0 && card.commands == before);
    reset(); put16(card.id + 98, 0); assert(attach() < 0 && !ata.ready);
    reset(); put32(card.id + 120, 0); assert(attach() < 0 && !ata.ready);
    reset(); put16(card.id, 0xffff); assert(attach() < 0 && !ata.ready);
    reset(); card.id[510] = 0xa5; assert(attach() < 0 && !ata.ready);
    reset(); put16(card.id + 212, 0x5000); put16(card.id + 234, 512); assert(attach() < 0 && !ata.ready);
    {
        static unsigned char batch[64 * 512];
        static const unsigned int counts[] = {1, 2, 8, 64};
        unsigned int n, count, sector, at, expected;
        for (n = 0; n < 4; ++n) {
            reset(); assert(attach() == 0); ata.issue_read_sectors = issue_many;
            card.synthetic = 1; count = counts[n]; before = card.commands;
            assert(pccf_ata_read(&ata, 4096, count, batch) == 0);
            assert(card.commands == before + 1 && ata.sector_reads == count && card.position == count * 256);
            for (sector = 0; sector < count; ++sector) for (at = 0; at < 256; ++at) {
                expected = ((4096 + sector) * 257 + at) & 0xffff;
                assert(batch[sector * 512 + at * 2] == (expected & 255));
                assert(batch[sector * 512 + at * 2 + 1] == (expected >> 8));
            }
        }
        for (f = DRQ_TIMEOUT; f <= FLOATING; ++f) {
            reset(); assert(attach() == 0); ata.issue_read_sectors = issue_many;
            card.synthetic = 1; card.fault = f; card.fault_command = 2; card.fault_sector = 3;
            assert(pccf_ata_read(&ata, 4096, 8, batch) < 0 && !ata.ready && card.position == 3 * 256);
            before = card.commands;
            assert(pccf_ata_read(&ata, 4096, 8, batch) < 0 && card.commands == before);
        }
        reset(); assert(attach() == 0); ata.issue_read_sectors = issue_many; card.synthetic = 1;
        assert(pccf_ata_read(&ata, 12224, 64, batch) == 0);
        before = card.commands;
        assert(pccf_ata_read(&ata, 12225, 64, batch) < 0 && card.commands == before);
    }
    puts("PASS: persistent ATA PIO reset map/width/order/256 words, capacity bounds, repeated reads, all faults/timeouts, IDENTIFY checks and offline latch");
    return 0;
}
