#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../iop/pccf/pcdiag_cis.h"

static unsigned char card[PCDIAG_CIS_BUS_LIMIT];
static char output[65536];
static unsigned int used, reads, position;
static unsigned char read_attribute(void *context, unsigned int bus)
{
    assert(context == card);
    assert(bus < sizeof(card) && (bus & 1) == 0);
    ++reads;
    return card[bus];
}
static void capture(const char *format, ...)
{
    va_list args;
    int size;
    assert(used + 14 < sizeof(output));
    memcpy(output + used, "[1.023 IOP] ", 12);
    used += 12;
    va_start(args, format);
    size = vsnprintf(output + used, sizeof(output) - used, format, args);
    va_end(args);
    assert(size >= 0 && (unsigned int)size < sizeof(output) - used - 1);
    used += size;
    output[used++] = '\n';
    output[used] = 0;
}
static void reset(void)
{
    unsigned int i;
    memset(card, 0xe7, sizeof(card));
    for (i = 0; i < sizeof(card); i += 2) card[i] = 0xff;
    used = reads = position = 0;
    output[0] = 0;
}
static void byte(unsigned int value)
{
    assert(position < sizeof(card));
    card[position] = value;
    position += 2;
}
static void tuple(unsigned int code, const unsigned char *payload, unsigned int length)
{
    unsigned int i;
    byte(code); byte(length);
    for (i = 0; i < length; ++i) byte(payload[i]);
}
static int dump(void)
{
    int result = pcdiag_cis_dump(read_attribute, card, capture);
    /* Leave room in the IOP's 32 KiB buffer for ordinary module messages. */
    assert(used < 32768 - 2048);
    return result;
}
int main(void)
{
    static const unsigned char manfid[] = {0x4f,0,0,0};
    static const unsigned char funcid[] = {4,0};
    static const unsigned char config[] = {1,7,0,2,0x0f};
    static const unsigned char memory_entry[] = {0xc0,0xc0,0};
    static const unsigned char io_entry[] = {0x81,0xc1,0};
    static const unsigned char version[] = {5,0,'K','I','N','G','S','T','O','N',0,'C','F',0,0xff};
    static const unsigned char high_address[] = {3,1,0x78,0x56,0x34,0x92,1};
    unsigned int i, seed = 0x1234abcd, trial;
    reset();
    byte(0); /* NULL padding has no length byte. */
    tuple(0x20,manfid,sizeof(manfid));
    tuple(0x21,funcid,sizeof(funcid)); /* Must keep reading beyond MANFID. */
    tuple(0x15,version,sizeof(version));
    tuple(0x1a,config,sizeof(config));
    tuple(0x1b,memory_entry,sizeof(memory_entry));
    tuple(0x1b,io_entry,sizeof(io_entry));
    byte(0xff);
    assert(dump() == 0);
    assert(strstr(output,"manufacturer=004f card=0000"));
    assert(strstr(output,"function=04 (fixed disk)"));
    assert(strstr(output,"KINGSTON|CF|"));
    assert(strstr(output,"base_attribute=00000200 last_index=7 mask_bytes=1 COR_present=1"));
    assert(strstr(output,"type=0 (memory)"));
    assert(strstr(output,"type=1 (IO)"));
    assert(strstr(output,"CIS DUMP END"));
    assert(reads == position/2);

    reset(); tuple(0x1a,high_address,sizeof(high_address)); byte(0xff);
    assert(dump() == 0 && strstr(output,"base_attribute=92345678"));
    reset(); tuple(0x1a,config,2); tuple(0x1b,memory_entry,1); byte(0xff);
    assert(dump() == 0 && strstr(output,"CONFIG malformed") && strstr(output,"missing interface"));
    reset(); tuple(0x11,high_address,4); byte(0xff);
    assert(dump() == 0 && strstr(output,"linked chain is not followed"));

    reset(); byte(0xff);
    assert(dump() == 0 && reads == 1); /* Never read after END. */
    reset(); byte(1); byte(0xff);
    assert(dump() == 0 && reads == 2 && strstr(output,"link=ff"));
    reset();
    for (i=0;i<sizeof(card);i+=2) card[i]=0;
    card[sizeof(card)-2]=1;
    assert(dump() == -1 && strstr(output,"missing length"));
    reset();
    for (i=0;i<sizeof(card);i+=2) card[i]=0;
    card[sizeof(card)-6]=1; card[sizeof(card)-4]=4;
    assert(dump() == -1 && strstr(output,"payload outside bound"));
    reset();
    for (i=0;i<sizeof(card);i+=2) card[i]=0;
    assert(dump() == -2 && strstr(output,"byte bound"));
    reset();
    for (i=0;i<PCDIAG_CIS_TUPLE_LIMIT+1;++i) tuple(0x1b,memory_entry,0);
    assert(dump() == -2 && strstr(output,"tuple limit"));

    /* Deterministic malformed CIS input checks address and output bounds. */
    for (trial=0;trial<300;++trial) {
        reset();
        for (i=0;i<sizeof(card);i+=2) {
            seed=seed*1664525U+1013904223U;
            card[i]=(unsigned char)(seed>>24);
        }
        dump();
    }
    puts("PASS: CIS continues past MANFID, decoding, stride, END, malformed tuples, link handling and bounded random input");
    return 0;
}
