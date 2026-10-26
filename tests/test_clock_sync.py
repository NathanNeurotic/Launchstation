"""Run production SNTP packets, failures, receipt-based elapsed time and RTC writes."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_clock_sync.c").read_text()
start = source.index("#define SNTP_PACKET_SIZE")
end = source.index("\n#else", start)

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef struct { u8 stat,second,minute,hour,pad,day,month,year; } sceCdCLOCK;
#define DPRINTF(...) ((void)0)
#define BASE_MS 1700000000000ULL
static u64 now_ms;
static int write_delay,write_timer_reset;
static u64 Timer(void) { return now_ms; }
static int network_ready,clock_write_ok,clock_read_ok,clock_writes,refreshes;
static sceCdCLOCK rtc;
static int ensureDashboardIpNetworkReady(void) { return network_ready; }
static const char *dashboardIpNetworkError(void) { return "network startup failed"; }
static void dashLoadingFrame(const char *s,int i,int n) {
    assert(s && strstr(s,"0.pool.ntp.org") && strstr(s,"attempt") && i>=0 && i<2 && n==2);
}
static int sceCdWriteClock(sceCdCLOCK *clock) { clock_writes++;rtc=*clock;return clock_write_ok; }
static int sceCdReadClock(sceCdCLOCK *clock) { *clock=rtc;return clock_read_ok; }
static void menuTitleUpdateAsync(int force) { assert(force);refreshes++; }
'''
code += source[start:end]
code += r'''
enum {
    F_NONE,F_DNS,F_DNS_LIST,F_DNS_ADDRESS,F_DNS_LENGTH,F_DNS_FAMILY,
    F_SOCKET,F_CONNECT,F_SEND,F_SEND_SHORT,F_WAIT,F_WAIT_EMPTY,F_TIMEOUT,F_RECEIVE,
    F_SHORT,F_MODE,F_STRATUM,F_UNSYNCED,F_REFUSED,F_ORIGIN,F_OLD,F_FUTURE,F_TIMER
};
typedef struct {
    int fault;
    u64 dns_delay,rtt,override_ms;
} Query;
static Query queries[2],*active_query;
static int query_calls,socket_calls,send_calls,select_calls,receive_calls,closes;
static int socket_open,socket_connected;
static char hosts[2][32];
static u8 request[48],address[4]={203,0,113,9};
static SntpSockaddrIn destination;
static void reset(void) {
    now_ms=10000;write_delay=write_timer_reset=0;
    network_ready=clock_write_ok=clock_read_ok=1;
    clock_writes=refreshes=query_calls=socket_calls=send_calls=select_calls=receive_calls=closes=0;
    socket_open=socket_connected=0;active_query=NULL;
    memset(queries,0,sizeof(queries));memset(hosts,0,sizeof(hosts));
    memset(&rtc,0,sizeof(rtc));
}
struct hostent *ps2ipc_gethostbyname(const char *name) {
    static SntpHostEnt host;
    static char *addresses[2];
    assert(query_calls<2 && !socket_open && !strcmp(name,"0.pool.ntp.org"));
    snprintf(hosts[query_calls],sizeof(hosts[0]),"%s",name);
    active_query=&queries[query_calls++];now_ms+=active_query->dns_delay;
    int fault=active_query->fault;
    if(fault==F_DNS)return NULL;
    addresses[0]=fault==F_DNS_ADDRESS?NULL:(char *)address;addresses[1]=NULL;
    host.h_addrtype=fault==F_DNS_FAMILY?AF_UNIX:AF_INET;
    host.h_length=fault==F_DNS_LENGTH?16:4;
    host.h_addr_list=fault==F_DNS_LIST?NULL:addresses;
    return (struct hostent *)(void *)&host;
}
int ps2ipc_socket(int family,int type,int protocol) {
    assert(family==AF_INET && type==SOCK_DGRAM && !protocol && !socket_open);
    socket_calls++;
    if(active_query->fault==F_SOCKET)return -1;
    socket_open=1;return 7;
}
int ps2ipc_connect(int sock,const struct sockaddr *to,int tolen) {
    assert(socket_open && !socket_connected && sock==7 && tolen==16);
    destination=*(const SntpSockaddrIn *)(const void *)to;
    assert(destination.sin_family==AF_INET && destination.sin_port==sntpHtons(123));
    assert(!memcmp(&destination.sin_addr,address,4));
    if(active_query->fault==F_CONNECT)return -1;
    socket_connected=1;return 0;
}
int ps2ipc_send(int sock,const void *data,int size,unsigned flags) {
    assert(socket_open && socket_connected && sock==7 && size==48 && !flags);
    memcpy(request,data,48);assert(request[0]==0x23 && getBe32(request+40));
    send_calls++;
    if(active_query->fault==F_SEND)return -1;
    return active_query->fault==F_SEND_SHORT?47:48;
}
int ps2ipc_select(int maxfdp1,fd_set *readset,fd_set *writeset,fd_set *exceptset,struct timeval *timeout) {
    assert(socket_open && socket_connected && maxfdp1==8 && FD_ISSET(7,readset) && !writeset && !exceptset);
    assert(timeout && timeout->tv_sec==5 && timeout->tv_usec==0);
    select_calls++;
    if(active_query->fault==F_WAIT)return -1;
    if(active_query->fault==F_TIMEOUT) { now_ms+=5000;FD_ZERO(readset);return 0; }
    if(active_query->fault==F_WAIT_EMPTY) { FD_ZERO(readset);return 1; }
    now_ms+=active_query->rtt;
    return 1;
}
int ps2ipc_recv(int sock,void *data,int size,unsigned flags) {
    assert(socket_open && socket_connected && sock==7 && size==48 && !flags);
    receive_calls++;
    if(active_query->fault==F_RECEIVE)return -1;
    u8 *reply=data;
    memset(reply,0,48);reply[0]=0x24;reply[1]=2;
    memcpy(reply+24,request+40,8);
    u64 remote=active_query->override_ms?active_query->override_ms:
               BASE_MS+now_ms-active_query->rtt/2;
    if(active_query->fault==F_OLD)remote=UNIX_2020*1000-1000;
    if(active_query->fault==F_FUTURE)remote=UNIX_2100*1000;
    u64 ntp_seconds=remote/1000+SNTP_EPOCH_DELTA;
    putBe32(reply+40,(u32)ntp_seconds);
    /* Round upward to preserve the intended millisecond when decoding. */
    putBe32(reply+44,(u32)((((remote%1000)<<32)+999)/1000));
    switch(active_query->fault) {
        case F_SHORT:return 47;
        case F_MODE:reply[0]=0x23;break;
        case F_STRATUM:reply[1]=16;break;
        case F_UNSYNCED:reply[0]|=0xc0;break;
        case F_REFUSED:reply[0]|=0xc0;reply[1]=0;memcpy(reply+12,"RATE",4);break;
        case F_ORIGIN:reply[24]^=1;break;
        case F_TIMER:now_ms=1;break;
    }
    return 48;
}
int ps2ipc_disconnect(int sock) {
    assert(sock==7 && socket_open);socket_open=socket_connected=0;closes++;
    now_ms+=write_delay;
    if(write_timer_reset)now_ms=1;
    return 0;
}
static u64 writtenUtc(void) {
    s64 seconds;assert(clockLocalSeconds(&rtc,&seconds));
    return (u64)seconds-9*3600;
}
static void checkQuery(void) {
    const int cases[][2]={
        {F_DNS,SNTP_DNS},{F_DNS_LIST,SNTP_DNS},{F_DNS_ADDRESS,SNTP_DNS},
        {F_DNS_LENGTH,SNTP_DNS},{F_DNS_FAMILY,SNTP_DNS},{F_SOCKET,SNTP_SOCKET},{F_CONNECT,SNTP_CONNECT},
        {F_SEND,SNTP_SEND},{F_SEND_SHORT,SNTP_SEND},{F_WAIT,SNTP_WAIT},
        {F_WAIT_EMPTY,SNTP_WAIT},{F_TIMEOUT,SNTP_TIMEOUT},{F_RECEIVE,SNTP_RECEIVE},
        {F_SHORT,SNTP_SHORT},{F_MODE,SNTP_INVALID},{F_STRATUM,SNTP_INVALID},
        {F_UNSYNCED,SNTP_UNSYNCED},{F_REFUSED,SNTP_REFUSED},{F_ORIGIN,SNTP_ORIGIN},
        {F_OLD,SNTP_DATE},{F_FUTURE,SNTP_DATE},{F_TIMER,SNTP_TIMER}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        reset();queries[0].fault=cases[i][0];
        SntpSample sample={123,456};
        assert(sntpQuery("0.pool.ntp.org",&sample)==cases[i][1]);
        assert(sample.unix_ms==123 && sample.received_ms==456 && !socket_open);
        assert(closes==(cases[i][0]>F_SOCKET));
        assert(strcmp(sntpStatusText(cases[i][1]),"not checked"));
    }
    reset();queries[0].rtt=1000;
    SntpSample sample;
    assert(sntpQuery("0.pool.ntp.org",&sample)==SNTP_OK);
    assert(sample.unix_ms==BASE_MS+now_ms-500 && sample.received_ms==now_ms && closes==1);
    /* Preserve era-1 support after the NTP timestamp wraps in 2036. */
    reset();queries[0].override_ms=2208988800000ULL;
    assert(sntpQuery("0.pool.ntp.org",&sample)==SNTP_OK);
    assert(sample.unix_ms==2208988800000ULL);
    assert(sntpQuery(NULL,&sample)==SNTP_INVALID);
    assert(sntpQuery("0.pool.ntp.org",NULL)==SNTP_INVALID);
    puts("PASS: SNTP connected UDP, echoed nonce, IPv4 lookup, fractional server time, receipt timer, era 1 and all query failure stages");
}
static void checkSingleSource(void) {
    char result[160];
    reset();
    assert(dashClockSyncInternet(result,sizeof(result)));
    assert(query_calls==1 && clock_writes==1 && refreshes==1);
    assert(!strcmp(hosts[0],"0.pool.ntp.org") && strstr(result,"synchronized"));
    assert(writtenUtc()==(BASE_MS+now_ms+500)/1000);
    /* DNS delay precedes the fresh reply; it must not be added to that time. */
    reset();queries[0].dns_delay=25000;
    assert(dashClockSyncInternet(result,sizeof(result)));
    assert(query_calls==1 && now_ms==35000 && writtenUtc()==(BASE_MS+now_ms)/1000);
    /* NTPS2's receipt-to-write tracking keeps the sample current after delay. */
    reset();write_delay=12500;
    assert(dashClockSyncInternet(result,sizeof(result)));
    assert(query_calls==1 && now_ms==22500 && writtenUtc()==(BASE_MS+now_ms+500)/1000);
    /* Use the received server timestamp directly, with no round-trip estimate. */
    reset();queries[0].rtt=4000;
    assert(dashClockSyncInternet(result,sizeof(result)));
    assert(writtenUtc()==(BASE_MS+now_ms-2000)/1000);
    /* Only a failed request gets a retry, always against the same hostname. */
    const int failures[]={F_DNS,F_SOCKET,F_CONNECT,F_SEND,F_WAIT,F_TIMEOUT,F_RECEIVE,F_SHORT,F_ORIGIN};
    for(unsigned i=0;i<sizeof(failures)/sizeof(failures[0]);i++) {
        reset();queries[0].fault=failures[i];
        assert(dashClockSyncInternet(result,sizeof(result)) && query_calls==2);
        assert(!strcmp(hosts[0],"0.pool.ntp.org") && !strcmp(hosts[1],hosts[0]));
        assert(writtenUtc()==(BASE_MS+now_ms+500)/1000);
    }
    reset();queries[1].fault=F_TIMEOUT;
    assert(dashClockSyncInternet(result,sizeof(result)) && query_calls==1);
    puts("PASS: one NTP Pool source, fresh time after DNS delay, elapsed time before writing, same-server retry and immediate success");
}
static void checkFailures(void) {
    char result[512];
    reset();network_ready=0;
    assert(!dashClockSyncInternet(result,sizeof(result)) && !query_calls && !clock_writes);
    assert(!strcmp(result,"network startup failed"));
    const int faults[]={F_DNS,F_SOCKET,F_CONNECT,F_SEND,F_WAIT,F_TIMEOUT,F_RECEIVE,F_SHORT,F_ORIGIN,F_OLD};
    const char *messages[]={"DNS lookup failed","socket creation failed","server connection failed","request send failed",
                           "socket wait failed","reply timed out","reply receive failed",
                           "short reply","request/reply mismatch","server date out of range"};
    for(unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);i++) {
        reset();queries[0].fault=queries[1].fault=faults[i];
        assert(!dashClockSyncInternet(result,sizeof(result)) && query_calls==2 && !clock_writes && !refreshes);
        assert(strstr(result,"0.pool.ntp.org:") && strstr(result,messages[i]));
    }
    reset();queries[0].fault=F_REFUSED;
    assert(!dashClockSyncInternet(result,sizeof(result)) && query_calls==1 && !clock_writes);
    assert(strstr(result,"server refused request"));
    reset();write_timer_reset=1;
    assert(!dashClockSyncInternet(result,sizeof(result)) && !clock_writes);
    assert(strstr(result,"Local timer restarted"));
    reset();clock_write_ok=0;
    assert(!dashClockSyncInternet(result,sizeof(result)) && clock_writes==1 && !refreshes);
    assert(strstr(result,"RTC write"));
    reset();clock_read_ok=0;
    assert(!dashClockSyncInternet(result,sizeof(result)) && clock_writes==1 && !refreshes);
    assert(strstr(result,"RTC write"));
    reset();assert(dashClockSyncInternet(NULL,0));
    reset();queries[0].fault=queries[1].fault=F_DNS;
    char small[9];memset(small,'X',sizeof(small));
    assert(!dashClockSyncInternet(small,8) && small[7]==0 && small[8]=='X');
    puts("PASS: query failure diagnostics, bounded retry, server refusals, timer restart, network startup, RTC failures and bounded result buffers");
}
int main(void) {
    checkQuery();checkSingleSource();checkFailures();
}
'''
compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="clock-sync-") as folder:
    path = Path(folder)
    (path / "sync.c").write_text(code)
    executable = path / "sync"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path / "sync.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=10)
