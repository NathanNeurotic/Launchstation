/* Stateful PIO fixture shared from the DIAG5 regression; intentionally
   retain the complete byte/word/command checks for the persistent engine. */
/* Actual ATA/FAT code with a stateful PIO card: transfer count, widths,
   byte order, command whitelist, timeouts and failures are observable. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../iop/pccf/pccf_ata.h"

enum { OK, BAD_MAP, REJECT_COMMAND, DRQ_TIMEOUT, DRQ_ERROR, DRQ_FAULT,
       FLOATING, COMPLETE_TIMEOUT, COMPLETE_ERROR, FINAL_ERROR };
typedef struct {
    unsigned int space, width, commands, reads, words, waits, position, alternate;
    unsigned int command, lba, fault, fault_command, fat, full_root, delay;
    unsigned int zero_signature, initial_status, bad_signature;
    unsigned int initial_busy_ms;
    unsigned int count, synthetic, phase_reads, fault_sector;
    unsigned char id[512], mbr[512], boot[512], root[512];
} card_t;
static card_t card;
static char log_buffer[32768];
static unsigned int log_used;
static void trace(const char *format, ...)
{
    va_list ap;
    char line[384];
    int count;
    va_start(ap, format); count = vsnprintf(line, sizeof(line), format, ap); va_end(ap);
    assert(count >= 0 && count < (int)sizeof(line));
    assert(log_used + (unsigned int)count + 1 < sizeof(log_buffer));
    memcpy(log_buffer + log_used, line, count);
    log_used += count; log_buffer[log_used++] = '\n'; log_buffer[log_used] = 0;
}
static void put16(unsigned char *p, unsigned int n) { p[0] = n; p[1] = n >> 8; }
static void put32(unsigned char *p, unsigned int n) { put16(p, n); put16(p + 2, n >> 16); }
static void ata_string(unsigned int word, unsigned int size, const char *s)
{
    unsigned int i, len = strlen(s);
    for (i = 0; i < size; ++i) card.id[word * 2 + (i ^ 1U)] = i < len ? s[i] : ' ';
}
static void reset(void)
{
    memset(&card, 0, sizeof(card)); log_used = 0; log_buffer[0] = 0;
    card.fault_command = 1;
    card.initial_status = 0x50;
    put16(card.id, 0x848a); put16(card.id + 98, 0x0200); put32(card.id + 120, 12288);
    ata_string(27, 40, "TRANSCEND TS4GCF133"); ata_string(10, 20, "SERIAL123"); ata_string(23, 8, "V1.0");
}
static void make_fat(int full)
{
    unsigned int i;
    card.fat = 1; card.full_root = full;
    card.mbr[510] = 0x55; card.mbr[511] = 0xaa; card.mbr[450] = 6;
    put32(card.mbr + 454, 2048); put32(card.mbr + 458, 8192);
    card.boot[0] = 0xeb; card.boot[2] = 0x90; put16(card.boot + 11, 512);
    card.boot[13] = 1; put16(card.boot + 14, 1); card.boot[16] = 2;
    put16(card.boot + 17, 512); put16(card.boot + 19, 8192); put16(card.boot + 22, 32);
    card.boot[510] = 0x55; card.boot[511] = 0xaa;
    for (i = 0; i < (full ? 16U : 1U); ++i) {
        unsigned char *entry = card.root + i * 32;
        memcpy(entry, "CFTEST  TXT", 11); entry[11] = 0x20;
        put16(entry + 26, 2); put32(entry + 28, 123);
    }
}
static int fault_active(void) { return card.commands == card.fault_command; }
static unsigned char read8(void *context, unsigned int offset)
{
    unsigned int fault;
    assert(context == &card && card.space == 0 && card.width == 8);
    if (!card.commands) {
        switch (offset) {
            case 7: case 0x0e: return card.waits < card.initial_busy_ms ? 0x80 : card.initial_status;
            case 2: return card.bad_signature ? 2 : (card.zero_signature ? 0 : 1);
            case 3: return card.zero_signature ? 0 : 1;
            case 4: case 5: return 0;
            case 6: return card.zero_signature ? 0 : 0xa0;
            default: assert(0);
        }
    }
    fault = fault_active() && card.position / 256 >= card.fault_sector ? card.fault : OK;
    if (offset == 1) return 4;
    if (offset == 7) return fault == FINAL_ERROR ? 0x51 : 0x50;
    assert(offset == 0x0e);
    ++card.alternate;
    if (card.position < card.count * 256) {
        if (fault == FLOATING) return 0xff;
        if (fault == DRQ_TIMEOUT) return 0x80;
        if (fault == DRQ_ERROR) return 0x51;
        if (fault == DRQ_FAULT) return 0x70;
        if (card.position && !(card.position % 256) && card.phase_reads++ < 6) return 0x80;
        if (card.delay && card.alternate < 10) return 0x81; /* ERR ignored while BSY. */
        return 0x58;
    }
    return fault == COMPLETE_TIMEOUT ? 0x80 : (fault == COMPLETE_ERROR ? 0x51 : 0x50);
}
static unsigned short read16(void *context, unsigned int offset)
{
    const unsigned char *data;
    unsigned int at;
    assert(context == &card && card.space == 0 && card.width == 16);
    if (!card.commands) { assert(offset == 6); return card.fault == BAD_MAP ? 0xa050 : (card.initial_status << 8) | (card.zero_signature ? 0 : 0xa0); }
    assert(offset == 0 && card.position < card.count * 256);
    if (card.synthetic && card.command == 0x20) {
        unsigned int value = ((card.lba + card.position / 256) * 257 + card.position % 256) & 0xffff;
        ++card.position; ++card.words;
        if (!(card.position % 256)) card.phase_reads = 0;
        return value;
    }
    if (card.command == 0xec) data = card.id;
    else if (card.lba == 0) data = card.mbr;
    else if (card.lba == 2048) { assert(card.fat); data = card.boot; }
    else { assert(card.fat && card.lba >= 2113 && card.lba < 2117); data = card.root; }
    at = (card.position++ % 256) * 2; ++card.words;
    if (!(card.position % 256)) card.phase_reads = 0;
    return (unsigned int)data[at] | ((unsigned int)data[at + 1] << 8);
}
static void set_access(void *context, unsigned int space, unsigned int width)
{
    assert(context == &card && space == 0 && (width == 8 || width == 16));
    card.space = space; card.width = width;
}
static void wait_ms(void *context, unsigned int ms)
{
    assert(context == &card && ms == 1); ++card.waits;
    assert(card.waits <= 5000 + PCDIAG_ATA_POLL_LIMIT_MS + 64);
}
static int issue(void *context, unsigned int command, unsigned int lba)
{
    assert(context == &card && card.space == 0 && card.width == 8);
    assert((command == 0xec && card.commands == 0 && lba == 0) ||
           (command == 0x20 && card.commands > 0 && lba < 12288));
    assert(card.commands == 0 || card.position == card.count * 256);
    ++card.commands;
    if (fault_active() && card.fault == REJECT_COMMAND) return -1;
    if (command == 0x20) ++card.reads;
    card.command = command; card.lba = lba; card.position = card.alternate = 0;
    card.count = 1; card.phase_reads = 0;
    return 0;
}
