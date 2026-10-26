"""Check the IRX build contract and execute the socket server's RPC boundary."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
module = root / "iop/ps2ips_rpc"
yaml = (root / "ps2.yaml").read_text()
server = (module / "ps2ips.c").read_text()


def check_build_config(config):
    # An EE library named ps2ips is not a driver artifact. Resolve the explicit
    # embedding alias to a local IOP target, rather than accepting its spelling.
    alias = re.search(r"^      - ([\w-]+):ps2ips$", config, re.M)
    assert alias, "Socket server needs an explicit local IRX alias"
    target = re.search(r"^  - name: " + re.escape(alias[1]) +
                       r"\n(.*?)(?=^  - name:|\Z)", config, re.M | re.S)
    assert target, "Socket server target is missing"
    body = target[1]
    assert re.search(r"^    type: iop$", body, re.M), "Socket server must produce an IRX"
    assert "sources: [iop/ps2ips_rpc/ps2ips.c]" in body
    assert "imports_lst: iop/ps2ips_rpc/imports.lst" in body
    assert "defines: [PS2IP_DNS]" in body
    assert re.search(r"libs: \[[^\n]*\bps2ips, cglue\b", config)


check_build_config(yaml)
for broken in (yaml.replace("ps2ips_rpc:ps2ips", "ps2ips"),
               yaml.replace("name: ps2ips_rpc\n    type: iop",
                            "name: ps2ips_rpc\n    type: ee_lib")):
    try:
        check_build_config(broken)
    except AssertionError:
        pass
    else:
        raise AssertionError("The old library-as-IRX configuration must be rejected")

isolated = (module / "ps2.yaml").read_text()
assert "name: ps2ips_rpc\n    type: iop" in isolated
assert "sources: [ps2ips.c]" in isolated and "defines: [PS2IP_DNS]" in isolated
workflow = (root / ".github/workflows/compile.yml").read_text()
assert "python3 tests/test_ps2ips_rpc.py" in workflow
assert "ps2build build -c iop/ps2ips_rpc/ps2.yaml" in workflow
assert "I_DeleteThread" in (module / "imports.lst").read_text()


def function(name):
    match = re.search(r"(?:static )?(?:void \* |void \*|void |int )" +
                      name + r"\([^)]*\)\n\{", server)
    assert match, name
    end = server.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (server[end] == "{") - (server[end] == "}")
        end += 1
    return server[match.start():end] + "\n"


code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ps2ip_rpc.h"
#define MODULE_RESIDENT_END 0
#define MODULE_NO_RESIDENT_END 1
#define TH_C 2
#define _start ps2ips_start
typedef struct { int value; } SifRpcDataQueue_t;
typedef struct { int value; } SifRpcServerData_t;
typedef struct {
    int attr,option;void (*thread)(void *);int stacksize,priority;
} iop_thread_t;
static SifRpcDataQueue_t ps2ips_queue;
static SifRpcServerData_t ps2ips_server;
static u8 _rpc_buffer[2048];
static int create_result,start_result,starts,deletes,registrations,loops;
static void (*thread_entry)(void *);
static void *(*rpc_handler)(unsigned int,void *,int);
static u8 *registered_buffer;
static int CreateThread(const iop_thread_t *t) {
    assert(t->attr==TH_C && t->option==0 && t->stacksize>=0x800);
    thread_entry=t->thread;return create_result;
}
static int StartThread(int id,void *arg) {
    assert(id==42 && !arg);starts++;
    if(start_result<0)return start_result;
    thread_entry(arg);return 0;
}
static int DeleteThread(int id) { assert(id==42);deletes++;return 0; }
static int GetThreadId(void) { return 7; }
static void sceSifSetRpcQueue(SifRpcDataQueue_t *queue,int id) {
    assert(queue==&ps2ips_queue && id==7);
}
static void sceSifRegisterRpc(SifRpcServerData_t *rpc,unsigned id,void *handler,
                             u8 *buf,void *a,void *b,SifRpcDataQueue_t *queue) {
    assert(rpc==&ps2ips_server && id==PS2IP_IRX && !a && !b && queue==&ps2ips_queue);
    registrations++;rpc_handler=handler;registered_buffer=buf;
}
static void sceSifRpcLoop(SifRpcDataQueue_t *queue) {
    assert(queue==&ps2ips_queue);loops++;
}
struct hostent { int h_addrtype,h_length;char **h_addr_list; };
static int lookup_case;
static unsigned char resolved_ip[]={203,0,113,9};
static struct hostent *gethostbyname(const char *name) {
    static char *addresses[2];static struct hostent host;
    assert(!strcmp(name,"time.example.test"));
    if(lookup_case==1)return NULL;
    addresses[0]=lookup_case==3?NULL:(char *)resolved_ip;addresses[1]=NULL;
    host.h_addrtype=AF_INET;host.h_length=lookup_case==4?3:4;
    host.h_addr_list=lookup_case==2?NULL:addresses;return &host;
}
static ip_addr_t dns[2];
static void dns_setserver(u8 slot,const ip_addr_t *address) {
    assert(slot<2);dns[slot]=*address;
}
static const ip_addr_t *dns_getserver(u8 slot) { assert(slot<2);return &dns[slot]; }
'''
tested = {"do_gethostbyname", "do_dns_setserver", "do_dns_getserver"}
# Keep production dispatch and its tested endpoints. Other socket operations
# must never be invoked by these narrow registration/DNS scenarios.
for name in re.findall(r"static void (do_\w+)\(", server):
    if name not in tested:
        code += f"static void {name}(void *b,int n) {{ (void)b;(void)n;assert(0); }}\n"
for name in ("do_gethostbyname", "do_dns_setserver", "do_dns_getserver",
             "rpcHandlerFunction", "threadRpcFunction", "_start"):
    code += function(name)
code += r'''
static void reset(void) {
    create_result=42;start_result=starts=deletes=registrations=loops=0;
    rpc_handler=NULL;registered_buffer=NULL;
}
int main(void) {
    reset();create_result=-10;
    assert(ps2ips_start(0,NULL)==MODULE_NO_RESIDENT_END);
    assert(!starts && !deletes && !registrations);
    reset();start_result=-11;
    assert(ps2ips_start(0,NULL)==MODULE_NO_RESIDENT_END);
    assert(starts==1 && deletes==1 && !registrations);
    reset();assert(ps2ips_start(0,NULL)==MODULE_RESIDENT_END);
    assert(starts==1 && !deletes && registrations==1 && loops==1);
    assert(rpc_handler && registered_buffer==_rpc_buffer);
    for(lookup_case=0;lookup_case<5;lookup_case++) {
        memset(_rpc_buffer,0,sizeof(_rpc_buffer));strcpy((char *)_rpc_buffer,"time.example.test");
        assert(rpc_handler(PS2IPS_ID_GETHOSTBYNAME,_rpc_buffer,256)==_rpc_buffer);
        gethostbyname_res_pkt *reply=(void *)_rpc_buffer;
        if(!lookup_case) {
            assert(!reply->result && reply->hostent.h_addrtype==AF_INET && reply->hostent.h_length==4);
            /* Return IP bytes, never the address of an IOP-side pointer. */
            assert(!memcmp(&reply->hostent.h_addr,resolved_ip,4));
        } else assert(reply->result==-1);
    }
    dns_setserver_pkt *request=(void *)_rpc_buffer;
    unsigned char resolver[]={1,1,1,1};
    memset(_rpc_buffer,0,sizeof(_rpc_buffer));request->numdns=1;
    memcpy(&request->dnsserver,resolver,4);
    rpc_handler(PS2IPS_ID_DNS_SETSERVER,_rpc_buffer,sizeof(*request));
    assert(!memcmp(&dns[1],resolver,4));
    _rpc_buffer[0]=1;
    rpc_handler(PS2IPS_ID_DNS_GETSERVER,_rpc_buffer,1);
    assert(!memcmp(&((dns_getserver_res_pkt *)_rpc_buffer)->dnsserver,resolver,4));
    puts("PASS: IRX target/client distinction, RPC registration, thread failures, DNS IPv4 bytes and resolver RPCs");
}
'''
compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="ps2ips-rpc-") as folder:
    path = Path(folder)
    (path / "tamtypes.h").write_text("#include <stdint.h>\ntypedef uint8_t u8;\n"
                                    "typedef int16_t s16;\ntypedef int32_t s32;\n"
                                    "typedef uint32_t u32;\n")
    (path / "tcpip.h").write_text("#include <sys/socket.h>\n#define MEMP_NUM_NETCONN 32\n"
                                 "typedef struct { u32 addr; } ip_addr_t;\n"
                                 "#define ip_addr_copy(d,s) ((d)=(s))\n")
    (path / "server.c").write_text(code)
    executable = path / "server"
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror","-DPS2IP_DNS",
                    "-I"+str(path),"-I"+str(module),str(path/"server.c"),
                    "-o",str(executable)],check=True)
    subprocess.run([str(executable)],check=True,timeout=10)


# Exercise the actual UDP/TCP transfer routines, including the request/reply
# buffer alias and the EE client's unaligned head/tail reassembly. A POSIX-only
# NTP socket mock cannot catch corruption in this SIF RPC layer.
transport = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ps2ip_rpc.h"
#define BUFF_SIZE 1024
#define MIN(a,b) (((a)<(b))?(a):(b))
#define RDOWN_64(a) (((a)>>6)<<6)
typedef struct { int unused; } SifRpcReceiveData_t;
struct t_SifDmaTransfer { void *src,*dest;int size,attr; };
"""
transport += re.search(r"static char lwip_buffer\[[^;]+;", server).group() + "\n"
transport += "static rests_pkt rests;\n"
transport += r"""
static union { s_recv_pkt rx;r_recv_pkt reply;send_pkt tx;u8 raw[512]; } shared;
static u8 ee[64+BUFF_SIZE+64] __attribute__((aligned(64)));
static u8 payload[BUFF_SIZE],sent[BUFF_SIZE];
static rests_pkt received_rest;
static u8 *expected_target;
static int wanted,wire_length,transfer_error,dma_calls,address_capacity,sent_length;
static struct sockaddr peer;
static int copy_incoming(void *data,int size) {
    assert(size==wanted && (char *)data>=lwip_buffer);
    assert((char *)data+size<=lwip_buffer+sizeof(lwip_buffer));
    if(transfer_error)return transfer_error;
    memcpy(data,payload,wire_length);return wire_length;
}
static int rpc_recvfrom(int socket,void *data,int size,int flags,struct sockaddr *from,int *fromlen) {
    assert(socket==7 && !flags && from && fromlen);
    address_capacity=*fromlen;
    int n=copy_incoming(data,size);
    if(n>0) { memcpy(from,&peer,sizeof(peer));*fromlen=sizeof(peer); }
    return n;
}
static int rpc_recv(int socket,void *data,int size,int flags) {
    assert(socket==7 && !flags);return copy_incoming(data,size);
}
static int copy_outgoing(const void *data,int size) {
    assert(size==wanted && (const char *)data>=lwip_buffer);
    assert((const char *)data+size<=lwip_buffer+sizeof(lwip_buffer));
    if(transfer_error)return transfer_error;
    memcpy(sent,data,size);sent_length=size;return size;
}
static int rpc_sendto(int socket,const void *data,int size,int flags,const struct sockaddr *to,int tolen) {
    assert(socket==7 && !flags && tolen==sizeof(peer) && !memcmp(to,&peer,sizeof(peer)));
    return copy_outgoing(data,size);
}
static int rpc_send(int socket,const void *data,int size,int flags) {
    assert(socket==7 && !flags);return copy_outgoing(data,size);
}
#define recvfrom rpc_recvfrom
#define recv rpc_recv
#define sendto rpc_sendto
#define send rpc_send
static int sceSifDmaStat(int id) { (void)id;return -1; }
static int CpuSuspendIntr(int *state) { *state=1;return 0; }
static int CpuResumeIntr(int state) { assert(state==1);return 0; }
static int sceSifSetDma(const struct t_SifDmaTransfer *dma,int count) {
    assert(count==1 && !dma->attr);dma_calls++;
    if(dma->src==&rests) {
        assert(dma->dest==&received_rest && dma->size==sizeof(rests));
    } else {
        assert((u8 *)dma->dest>=expected_target && (u8 *)dma->dest+dma->size<=expected_target+wire_length);
        assert((char *)dma->src>=lwip_buffer && (char *)dma->src+dma->size<=lwip_buffer+sizeof(lwip_buffer));
    }
    memcpy(dma->dest,dma->src,dma->size);return dma_calls;
}
static int sceSifGetOtherData(SifRpcReceiveData_t *data,void *source,void *dest,int size,int mode) {
    (void)data;assert(size>=0 && !mode);
    assert((char *)dest>=lwip_buffer && (char *)dest+size<=lwip_buffer+sizeof(lwip_buffer));
    memcpy(dest,source,size);return 0;
}
"""
for name in ("do_recvfrom", "do_recv", "do_sendto", "do_send"):
    transport += function(name)
transport += r"""
static void reset_transfer(int offset,int length) {
    memset(&shared,0,sizeof(shared));memset(ee,0xcc,sizeof(ee));
    memset(&received_rest,0,sizeof(received_rest));memset(&rests,0,sizeof(rests));
    memset(&peer,0x5a,sizeof(peer));peer.sa_family=AF_INET;
    expected_target=ee+64+offset;wanted=length;wire_length=length;
    transfer_error=dma_calls=sent_length=address_capacity=0;
    for(int i=0;i<BUFF_SIZE;i++)payload[i]=(u8)(i*43+17);
    payload[0]=0x24;payload[1]=2; /* NTP v4 server reply, stratum two. */
}
static void check_receive(int udp,int offset,int length,int actual,int error) {
    reset_transfer(offset,length);wire_length=actual;transfer_error=error;
    shared.rx=(s_recv_pkt){7,length,0,expected_target,&received_rest};
    (udp?do_recvfrom:do_recv)(&shared,sizeof(shared.rx));
    assert(shared.reply.ret==(error?error:actual));
    if(error || actual==0) { assert(!dma_calls);return; }
    if(udp) {
        assert(!memcmp(&shared.reply.sockaddr,&peer,sizeof(peer)));
    }
    /* Model the SDK EE receive callback. Check pointers before dereferencing:
     * the old UDP code overwrites them with the peer sockaddr before DMA. */
    assert(received_rest.sbuf==expected_target);
    if(udp)assert(address_capacity==sizeof(peer));
    assert(received_rest.ssize>=0 && received_rest.ssize<=64);
    assert(received_rest.esize>=0 && received_rest.esize<64);
    if(received_rest.ssize)memcpy(received_rest.sbuf,received_rest.sbuffer,received_rest.ssize);
    if(received_rest.esize) {
        assert(received_rest.ebuf>=expected_target && received_rest.ebuf+received_rest.esize<=expected_target+actual);
        memcpy(received_rest.ebuf,received_rest.ebuffer,received_rest.esize);
    }
    assert(!memcmp(expected_target,payload,actual)); /* Includes all eight echoed-origin bytes. */
    for(u8 *p=ee;p<expected_target;p++)assert(*p==0xcc);
    for(u8 *p=expected_target+actual;p<ee+sizeof(ee);p++)assert(*p==0xcc);
}
static void check_send(int udp,int offset,int length,int error) {
    reset_transfer(offset,length);transfer_error=error;
    memcpy(expected_target,payload,length);
    int misalignment=(uintptr_t)expected_target&63;
    int head=misalignment?MIN(64-misalignment,length):0;
    shared.tx=(send_pkt){.socket=7,.length=length,.flags=0,.ee_addr=expected_target,
                         .sockaddr=peer,.malign=head};
    memcpy(shared.tx.malign_buff,expected_target,head);
    (udp?do_sendto:do_send)(&shared,sizeof(shared.tx));
    int result;memcpy(&result,&shared,sizeof(result));
    assert(result==(error?error:length));
    if(!error)assert(sent_length==length && !memcmp(sent,payload,length));
}
int main(void) {
    const int lengths[]={1,47,48,63,64,65,127,128,960,1024};
    check_receive(1,0,48,48,0); /* Reproduce the NTP reply before other transfers. */
    for(int udp=0;udp<2;udp++)for(int offset=0;offset<64;offset++) {
        for(unsigned l=0;l<sizeof(lengths)/sizeof(lengths[0]);l++) {
            int length=lengths[l];
            check_receive(udp,offset,length,length,0);
            check_send(udp,offset,length,0);
        }
        check_receive(udp,offset,128,48,0); /* Short NTP datagram into a larger buffer. */
        check_receive(udp,offset,48,0,0);
        check_receive(udp,offset,48,0,-1);
        check_send(udp,offset,48,-1);
    }
    puts("PASS: real UDP/TCP RPC transfers preserve NTP origin bytes, peer address, unaligned buffers, full-size payloads and guards");
}
"""
with tempfile.TemporaryDirectory(prefix="ps2ips-transfer-") as folder:
    path = Path(folder)
    (path / "tamtypes.h").write_text("#include <stdint.h>\ntypedef uint8_t u8;\n"
                                    "typedef int16_t s16;\ntypedef int32_t s32;\n"
                                    "typedef uint32_t u32;\n")
    (path / "tcpip.h").write_text("#include <sys/socket.h>\n#define MEMP_NUM_NETCONN 32\n"
                                 "typedef struct { u32 addr; } ip_addr_t;\n")
    (path / "transfer.c").write_text(transport)
    executable = path / "transfer"
    # Production pointer arithmetic targets a 32-bit IOP. Place host globals
    # below 2 GiB to exercise those routines without changing their arithmetic.
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                    "-Wno-pointer-to-int-cast","-Wno-int-to-pointer-cast",
                    "-fno-pie","-no-pie","-DPS2IP_DNS","-I"+str(path),
                    "-I"+str(module),str(path/"transfer.c"),"-o",str(executable)],check=True)
    subprocess.run([str(executable)],check=True,timeout=10)
