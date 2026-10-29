/**
 * @file
 * PS2IP RPC definitions
 * This header conflicts with fileXio.h
 */

#ifndef __PS2IP_RPC_H__
#define __PS2IP_RPC_H__

#include <tamtypes.h>
#ifdef _EE
/* EE libcglue owns socket/config types. Its sys/socket.h must not be mixed
 * with the legacy tcpip.h used by IOP. DNS packets embed an IPv4 address;
 * libcglue only forward-declares that type, so complete its matching tag. */
#include <sys/socket.h>
#ifndef LWIP_HDR_IP4_ADDR_H
struct ip4_addr {
    u32 addr;
};
typedef struct ip4_addr ip4_addr_t;
typedef struct ip4_addr ip_addr_t;
#endif
#else
#include <tcpip.h>
#endif
#include <sys/time.h>

/* Match the embedded IOP stack's default select bitmap without importing
 * its legacy socket declarations into EE. Honor an active stack config. */
#ifndef MEMP_NUM_UDP_PCB
#define MEMP_NUM_UDP_PCB 4
#endif
#ifndef MEMP_NUM_TCP_PCB
#define MEMP_NUM_TCP_PCB 5
#endif
#ifndef MEMP_NUM_NETCONN
#define MEMP_NUM_NETCONN (MEMP_NUM_TCP_PCB + MEMP_NUM_UDP_PCB)
#endif

#define PS2IP_IRX 0xB0125F2

enum PS2IPS_RPC_ID {
    PS2IPS_ID_ACCEPT = 1,
    PS2IPS_ID_BIND,
    PS2IPS_ID_DISCONNECT,
    PS2IPS_ID_CONNECT,
    PS2IPS_ID_LISTEN,
    PS2IPS_ID_RECV,
    PS2IPS_ID_RECVFROM,
    PS2IPS_ID_SEND,
    PS2IPS_ID_SENDTO,
    PS2IPS_ID_SOCKET,
    PS2IPS_ID_SETCONFIG,
    PS2IPS_ID_GETCONFIG,
    PS2IPS_ID_SELECT,
    PS2IPS_ID_IOCTL,
    PS2IPS_ID_GETSOCKNAME,
    PS2IPS_ID_GETPEERNAME,
    PS2IPS_ID_GETSOCKOPT,
    PS2IPS_ID_SETSOCKOPT,
    PS2IPS_ID_GETHOSTBYNAME,
#ifdef PS2IP_DNS
    /*    Not implemented:
            gethostbyname_r -> Redudant because it gets called over the RPC.
            freeaddrinfo    -> too complicated and probably nobody uses it?
            getaddrinfo     -> too complicated and probably nobody uses it?    */

    PS2IPS_ID_DNS_SETSERVER,
    PS2IPS_ID_DNS_GETSERVER,
#endif

    PS2IPS_ID_COUNT
};

typedef struct
{
    s32 domain;
    s32 type;
    s32 protocol;
} socket_pkt;

typedef struct
{
    s32 ssize;
    s32 esize;
    u8 *sbuf;
    u8 *ebuf;
    u8 sbuffer[64];
    u8 ebuffer[64];
} rests_pkt;

typedef struct
{
    s32 socket;
    s32 length;
    s32 flags;
    void *ee_addr;
    struct sockaddr sockaddr; // sizeof = 16
    s32 malign;
    /** buffer for sending misaligned portion */
    u8 malign_buff[64];
} send_pkt;

typedef struct
{
    s32 socket;
    s32 length;
    s32 flags;
    void *ee_addr;
    void *intr_data;
} s_recv_pkt;

typedef struct
{
    s32 ret;
    struct sockaddr sockaddr;
} r_recv_pkt;

typedef struct
{
    s32 socket;
    struct sockaddr sockaddr;
    s32 len;
} cmd_pkt;

typedef struct
{
    s32 retval;
    struct sockaddr sockaddr;
} ret_pkt;

typedef struct
{
    s32 s;
    s32 backlog;
} listen_pkt;

typedef struct
{
    s32 s;
    s32 level;
    s32 optname;
} getsockopt_pkt;

typedef struct
{
    s32 result;
    s32 optlen;
    u8 buffer[128];
} getsockopt_res_pkt;

typedef struct
{
    s32 s;
    s32 level;
    s32 optname;
    s32 optlen;
    u8 buffer[128];
} setsockopt_pkt;

/* On-the-wire fd_set for the select RPC. The struct fd_set seen on EE
 * (newlib, ~128 bytes) and IOP (lwIP, MEMP_NUM_NETCONN/8 bytes) compile
 * units differs in size, which used to scramble select_pkt's layout
 * across the SIF RPC boundary — IOP would only touch the first few
 * bytes of EE's wider readset and never see writeset/exceptset at the
 * right offsets. Using a fixed-size struct here keeps select_pkt
 * byte-identical on both sides regardless of which struct fd_set is
 * in scope at the include site. */
typedef struct
{
    unsigned char fd_bits[(MEMP_NUM_NETCONN + 7) / 8];
} ps2ip_rpc_fd_set;

/* struct timeval differs across the boundary too — newlib's tv_sec/tv_usec
 * are 64-bit on EE while the IOP defines them as 32-bit longs. Carry
 * explicit 32-bit fields here so the layout is byte-identical on both
 * sides; each end reconstructs its native struct timeval. */
typedef struct
{
    union
    {
        s32 maxfdp1;
        s32 result;
    };
    void *timeout_p;     /* opaque NULL/non-NULL flag */
    s32 timeout_sec;
    s32 timeout_usec;
    void *readset_p;     /* opaque: only NULL/non-NULL is meaningful across RPC */
    void *writeset_p;
    void *exceptset_p;
    ps2ip_rpc_fd_set readset;
    ps2ip_rpc_fd_set writeset;
    ps2ip_rpc_fd_set exceptset;
} select_pkt;

typedef struct
{
    union
    {
        s32 s;
        s32 result;
    };
    u32 cmd;
    void *argp;
    u32 value;
} ioctl_pkt;

#ifdef PS2IP_DNS
struct hostent_res
{
    s16 h_addrtype;
    s16 h_length;
    /* h_addr is a lwIP macro expanding to h_addr_list[0]. */
    ip_addr_t address;
};

typedef struct
{
    s32 result;
    struct hostent_res hostent;
} gethostbyname_res_pkt;

typedef struct
{
    ip_addr_t dnsserver;
    u8 numdns;
} dns_setserver_pkt;

typedef struct
{
    ip_addr_t dnsserver;
} dns_getserver_res_pkt;

#endif

#endif /* __PS2IP_RPC_H__ */
