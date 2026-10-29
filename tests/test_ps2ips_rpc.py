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
client = (root / "ee_libs/tcpips/ps2ipc.c").read_text()
assert "      - ee_libs/tcpips/ps2ipc.c\n" in yaml
assert '#include "../../iop/ps2ips_rpc/ps2ip_rpc.h"' in client


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


def function(name, source=server):
    match = re.search(r"(?:static )?(?:void \* |void \*|void |int )" +
                      name + r"\([^)]*\)\n\{", source)
    assert match, name
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


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
            assert(!memcmp(&reply->hostent.address,resolved_ip,4));
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

# Compile the WHOLE EE client with the SDK's split header contract. Previous
# tests extracted receive functions and never saw the conflicting include chain.
with tempfile.TemporaryDirectory(prefix="ps2ips-ee-headers-") as folder:
    path = Path(folder)
    (path / "sys").mkdir()
    (path / "tamtypes.h").write_text("#pragma once\n#include <stdint.h>\n"
                                    "typedef uint8_t u8;typedef int16_t s16;\n"
                                    "typedef int32_t s32;typedef uint32_t u32;\n")
    (path / "tcpip.h").write_text('#error "Legacy IOP socket types must not reach EE"\n')
    (path / "sys/socket.h").write_text(r'''
#pragma once
#include <tamtypes.h>
typedef u32 socklen_t;
struct sockaddr {u8 sa_len,sa_family;char sa_data[14];};
struct in_addr {u32 s_addr;};
struct ip4_addr;
typedef struct ip4_addr ip4_addr_t;
typedef struct ip4_addr ip_addr_t;
#define h_addr h_addr_list[0]
typedef struct {char netif_name[4];struct in_addr ipaddr,netmask,gw;
               u32 dhcp_enabled,dhcp_status;u8 hw_addr[8];} t_ip_info;
struct fd_set {unsigned long bits[16];};
#define FD_ZERO(p) memset((p),0,sizeof(*(p)))
#define FD_ISSET(n,p) (((p)->bits[(n)/64] >> ((n)%64)) & 1UL)
#define FD_SET(n,p) ((p)->bits[(n)/64] |= 1UL << ((n)%64))
''')
    (path / "sys/time.h").write_text("#pragma once\nstruct timeval {long tv_sec,tv_usec;};\n")
    (path / "ps2ips.h").write_text("#pragma once\n#include <sys/socket.h>\n"
                                   "int ps2ip_init(void);void ps2ip_deinit(void);\n")
    (path / "kernel.h").write_text(r'''
#pragma once
#include <tamtypes.h>
typedef struct {int init_count,max_count;u32 option;int attr;} ee_sema_t;
int CreateSema(ee_sema_t *);
int WaitSema(int);int SignalSema(int);int DeleteSema(int);
void nopdelay(void);
#define UNCACHED_SEG(p) ((void *)(p))
#define IS_UNCACHED_SEG(p) 0
''')
    (path / "sifrpc.h").write_text(r'''
#pragma once
typedef struct {void *server;} SifRpcClientData_t;
int sceSifBindRpc(SifRpcClientData_t *,unsigned,int);
int sceSifCallRpc(SifRpcClientData_t *,unsigned,int,void *,int,void *,int,
                  void (*)(void *),void *);
void sceSifWriteBackDCache(void *,int);
''')
    (path / "iopcontrol.h").write_text("int HasIopRebootedSinceLastCall(void);\n")
    (path / "string.h").write_text("#include_next <string.h>\n"
                                  "size_t strlcpy(char *,const char *,size_t);\n")
    flags = [compiler,"-std=c11","-Wall","-Werror",
             "-Wno-pointer-to-int-cast","-D_EE","-I"+str(path),
             "-I"+str(module),"-fsyntax-only"]
    subprocess.run(flags + [str(root / "ee_libs/tcpips/ps2ipc.c")],check=True)
    (path / "layout.c").write_text(r'''
#include <stddef.h>
#include "ps2ip_rpc.h"
_Static_assert(sizeof(ip_addr_t)==4,"IPv4 wire address");
_Static_assert(sizeof(t_ip_info)==32,"IP configuration ABI");
_Static_assert(sizeof(struct sockaddr)==16,"socket address ABI");
_Static_assert(sizeof(gethostbyname_res_pkt)==12,"DNS reply includes IPv4 bytes");
_Static_assert(sizeof(ps2ip_rpc_fd_set)==EXPECTED_BITMAP,"select wire bitmap");
_Static_assert(offsetof(select_pkt,writeset)-offsetof(select_pkt,readset)==EXPECTED_BITMAP,
               "select field offsets must not use EE fd_set size");
''')
    for config in (["-DEXPECTED_BITMAP=2"],
                   ["-DMEMP_NUM_NETCONN=40","-DEXPECTED_BITMAP=5"]):
        subprocess.run(flags + ["-DPS2IP_DNS"] + config + [str(path/"layout.c")],check=True)
    print("PASS: complete EE client uses libcglue types, complete DNS views and matching/default-or-configured select bitmap")

with tempfile.TemporaryDirectory(prefix="ps2ips-rpc-") as folder:
    path = Path(folder)
    (path / "tamtypes.h").write_text("#include <stdint.h>\ntypedef uint8_t u8;\n"
                                    "typedef int16_t s16;\ntypedef int32_t s32;\n"
                                    "typedef uint32_t u32;\n")
    (path / "tcpip.h").write_text("#include <sys/socket.h>\n#define MEMP_NUM_NETCONN 32\n"
                                 "#define h_addr h_addr_list[0]\n"
                                 "typedef struct { u32 addr; } ip_addr_t;\n"
                                 "#define ip_addr_copy(d,s) ((d)=(s))\n")
    # CI searches SDK/common before our module directory. Preprocess the
    # production server's actual includes so a same-named SDK wire header
    # cannot silently replace the header used by the owned EE client.
    sdk = path / "sdk"
    sdk.mkdir()
    for name in ("types.h", "loadcore.h", "sifman.h", "sifcmd.h", "sysclib.h",
                 "thbase.h", "intrman.h", "ps2ip.h"):
        (sdk / name).write_text("")
    (sdk / "ps2ip_rpc.h").write_text('#error "SDK RPC header selected instead of the owned wire format"\n')
    subprocess.run([compiler,"-E","-D_IOP","-DPS2IP_DNS","-I"+str(sdk),
                    "-I"+str(path),"-I"+str(module),str(module/"ps2ips.c"),
                    "-o",str(path/"includes.i")],check=True)
    print("PASS: production IOP includes keep the owned RPC header ahead of a conflicting SDK copy")
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
static int pending_dma;
static struct t_SifDmaTransfer queued_dma;
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
static int sceSifDmaStat(int id) {
    if(pending_dma && id==pending_dma) {
        memcpy(queued_dma.dest,queued_dma.src,queued_dma.size);pending_dma=0;
    }
    return -1;
}
static int CpuSuspendIntr(int *state) { *state=1;return 0; }
static int CpuResumeIntr(int state) { assert(state==1);return 0; }
static int sceSifSetDma(const struct t_SifDmaTransfer *dma,int count) {
    assert(count==1 && !dma->attr && !pending_dma);dma_calls++;
    if(dma->src==&rests) {
        assert(dma->dest==&received_rest && dma->size==sizeof(rests));
    } else {
        assert((u8 *)dma->dest>=expected_target && (u8 *)dma->dest+dma->size<=expected_target+wire_length);
        assert((char *)dma->src>=lwip_buffer && (char *)dma->src+dma->size<=lwip_buffer+sizeof(lwip_buffer));
    }
    queued_dma=*dma;pending_dma=dma_calls;return dma_calls;
}
static int sceSifGetOtherData(SifRpcReceiveData_t *data,void *source,void *dest,int size,int mode) {
    (void)data;assert(size>=0 && !mode);
    assert((char *)dest>=lwip_buffer && (char *)dest+size<=lwip_buffer+sizeof(lwip_buffer));
    memcpy(dest,source,size);return 0;
}
"""
for name in ("do_recvfrom", "do_recv", "do_sendto", "do_send"):
    transport += function(name)
transport += re.search(r"static u8 _intr_data\[[^;]+;", client).group() + "\n"
transport += r"""
typedef struct { int server; } SifRpcClientData_t;
static SifRpcClientData_t _ps2ip={1};
static int lock_sema=1,locked,legacy_no_data;
static union { s_recv_pkt s_recv_pkt;r_recv_pkt r_recv_pkt; } _rpc_buffer;
#define UNCACHED_SEG(p) (p)
#define IS_UNCACHED_SEG(p) ((void)(p),0)
static void WaitSema(int sema) { assert(sema==1 && !locked);locked=1; }
static void SignalSema(int sema) { assert(sema==1 && locked);locked=0; }
static void sceSifWriteBackDCache(void *mem,int n) { assert(mem==expected_target && n==wanted); }
static int sceSifCallRpc(SifRpcClientData_t *rpc,int command,int mode,void *request,int size,
                         void *reply,int reply_size,void (*callback)(void *),void *arg) {
    assert(rpc==&_ps2ip && locked && !mode && size==sizeof(s_recv_pkt));
    assert(reply_size==sizeof(r_recv_pkt) && arg==_intr_data);
    assert(sizeof(_intr_data)>=sizeof(rests_pkt));
    assert(command==PS2IPS_ID_RECV || command==PS2IPS_ID_RECVFROM);
    memcpy(&shared.rx,request,size);assert(shared.rx.intr_data==_intr_data);
    shared.rx.intr_data=&received_rest;
    if(legacy_no_data) {
        assert(transfer_error==-1 && !wire_length);shared.reply.ret=-1;
    } else {
        (command==PS2IPS_ID_RECV?do_recv:do_recvfrom)(&shared,size);
        assert(!pending_dma); /* Metadata must finish before the EE callback. */
    }
    memcpy(reply,&shared.reply,reply_size);
    memcpy(_intr_data,&received_rest,sizeof(received_rest));
    callback(arg);return 0;
}
"""
for name in ("recv_intr", "ps2ipc_recv", "ps2ipc_recvfrom"):
    transport += function(name, client)
transport += r"""
static void reset_transfer(int offset,int length) {
    assert(!pending_dma);
    memset(&shared,0,sizeof(shared));memset(ee,0xcc,sizeof(ee));
    memset(&received_rest,0,sizeof(received_rest));memset(&rests,0,sizeof(rests));
    memset(&peer,0x5a,sizeof(peer));peer.sa_family=AF_INET;
    expected_target=ee+64+offset;wanted=length;wire_length=length;
    transfer_error=dma_calls=sent_length=address_capacity=legacy_no_data=0;
    for(int i=0;i<BUFF_SIZE;i++)payload[i]=(u8)(i*43+17);
    payload[0]=0x24;payload[1]=2; /* NTP v4 server reply, stratum two. */
}
static void check_receive(int udp,int offset,int length,int actual,int error) {
    reset_transfer(offset,length);wire_length=actual;transfer_error=error;
    shared.rx=(s_recv_pkt){7,length,0,expected_target,&received_rest};
    (udp?do_recvfrom:do_recv)(&shared,sizeof(shared.rx));
    assert(!pending_dma);
    assert(shared.reply.ret==(error?error:actual));
    if(error || actual==0) {
        assert(dma_calls==1 && !received_rest.ssize && !received_rest.esize);
        return;
    }
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
static void check_receive_retry(int udp) {
    check_receive(udp,13,1024,1024,0);
    /* Nonblocking retries and EOF must not replay the previous transfer's
     * head/tail into memory that TLS has already decrypted or freed. */
    memset(ee,0xa7,sizeof(ee));
    for(int result=0;result>=-1;result--) {
        shared.rx=(s_recv_pkt){7,1024,0,expected_target,&received_rest};
        wire_length=0;transfer_error=result;
        (udp?do_recvfrom:do_recv)(&shared,sizeof(shared.rx));
        assert(!pending_dma);
        assert(shared.reply.ret==result);
        assert(!received_rest.ssize && !received_rest.esize);
        for(unsigned i=0;i<sizeof(ee);i++)assert(ee[i]==0xa7);
    }
}
static int client_receive(int udp) {
    struct sockaddr from;int fromlen=sizeof(from);
    int result=udp?ps2ipc_recvfrom(7,expected_target,wanted,0,&from,&fromlen):
                   ps2ipc_recv(7,expected_target,wanted,0);
    assert(!locked && !pending_dma);
    if(udp && result>0)assert(fromlen==sizeof(peer) && !memcmp(&from,&peer,sizeof(peer)));
    return result;
}
static void check_client_receive(int udp,int offset,int length) {
    reset_transfer(offset,length);
    assert(client_receive(udp)==length && !memcmp(expected_target,payload,length));
    for(u8 *p=ee;p<expected_target;p++)assert(*p==0xcc);
    for(u8 *p=expected_target+length;p<ee+sizeof(ee);p++)assert(*p==0xcc);
    /* Even an old server's stale metadata must be ignored after a failed read. */
    memset(ee,0xa7,sizeof(ee));wire_length=0;transfer_error=-1;legacy_no_data=1;
    assert(client_receive(udp)==-1);
    for(unsigned i=0;i<sizeof(ee);i++)assert(ee[i]==0xa7);
    /* The owned server clears metadata on retries and EOF; a new read works. */
    legacy_no_data=0;
    assert(client_receive(udp)==-1);
    transfer_error=0;assert(client_receive(udp)==0);
    wire_length=length;
    assert(client_receive(udp)==length && !memcmp(expected_target,payload,length));
}
int main(void) {
    const int lengths[]={1,47,48,63,64,65,127,128,960,1024};
    check_receive(1,0,48,48,0); /* Reproduce the NTP reply before other transfers. */
    for(int udp=0;udp<2;udp++)for(int offset=0;offset<64;offset++) {
        for(unsigned l=0;l<sizeof(lengths)/sizeof(lengths[0]);l++) {
            int length=lengths[l];
            check_receive(udp,offset,length,length,0);
            check_send(udp,offset,length,0);
            check_client_receive(udp,offset,length);
        }
        check_receive(udp,offset,128,48,0); /* Short NTP datagram into a larger buffer. */
        check_receive(udp,offset,48,0,0);
        check_receive(udp,offset,48,0,-1);
        check_send(udp,offset,48,-1);
    }
    reset_transfer(0,48);wire_length=wanted=48;
    assert(ps2ipc_recvfrom(7,expected_target,wanted,0,NULL,NULL)==48);
    struct sockaddr address;
    assert(ps2ipc_recvfrom(7,expected_target,wanted,0,&address,NULL)==48);
    assert(!memcmp(&address,&peer,sizeof(peer)));
    int capacity=0;
    assert(ps2ipc_recvfrom(7,expected_target,wanted,0,NULL,&capacity)==48);
    assert(capacity==sizeof(peer));
    unsigned char tiny[2]={0xcc,0xa7};capacity=1;
    assert(ps2ipc_recvfrom(7,expected_target,wanted,0,(struct sockaddr *)tiny,&capacity)==48);
    assert(tiny[0]==((unsigned char *)&peer)[0] && tiny[1]==0xa7 && capacity==sizeof(peer));
    check_receive_retry(0);
    check_receive_retry(1);
    puts("PASS: real EE/IOP UDP/TCP transfers preserve bytes, metadata capacity, DMA ordering, unaligned buffers and guards across retries/EOF and fresh reads");
}
"""
with tempfile.TemporaryDirectory(prefix="ps2ips-transfer-") as folder:
    path = Path(folder)
    (path / "tamtypes.h").write_text("#include <stdint.h>\ntypedef uint8_t u8;\n"
                                    "typedef int16_t s16;\ntypedef int32_t s32;\n"
                                    "typedef uint32_t u32;\n")
    (path / "tcpip.h").write_text("#include <sys/socket.h>\n#define MEMP_NUM_NETCONN 32\n"
                                 "#define h_addr h_addr_list[0]\n"
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

# Oversized socket options must fail before locking or touching the RPC buffer.
options = r"""
#include <assert.h>
#include <string.h>
#include <stddef.h>
typedef unsigned int u32; typedef unsigned int socklen_t;
typedef int s32;
#define PS2IPS_ID_SETSOCKOPT 1
typedef struct {int s,level,optname;u32 optlen;char buffer[128];} setsockopt_pkt;
static struct {int server;} _ps2ip={1};
static union {setsockopt_pkt setsockopt_pkt;s32 result;} _rpc_buffer;
static int lock_sema,locks,rpcs;
static void WaitSema(int s) {(void)s;locks++;}
static void SignalSema(int s) {(void)s;locks--;}
static int sceSifCallRpc(void *c,int cmd,int mode,void *req,int n,void *reply,int nr,void *cb,void *arg) {
 (void)c;(void)cmd;(void)mode;(void)req;(void)n;(void)nr;(void)cb;(void)arg;
 assert(locks==1);rpcs++;*(int *)reply=0;return 0;
}
"""
options += function("ps2ipc_setsockopt", client)
options += r"""
int main(void) {
 char value[129];memset(value,7,sizeof(value));
 assert(ps2ipc_setsockopt(1,2,3,value,129)==-1 && !locks && !rpcs);
 assert(ps2ipc_setsockopt(1,2,3,NULL,1)==-1 && !locks && !rpcs);
 assert(!ps2ipc_setsockopt(1,2,3,value,128) && !locks && rpcs==1);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="ps2ips-options-") as folder:
    path=Path(folder)
    (path/"options.c").write_text(options)
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",str(path/"options.c"),"-o",str(path/"options")],check=True)
    subprocess.run([str(path/"options")],check=True)
print("PASS: socket option bounds reject oversize and null data before RPC")
