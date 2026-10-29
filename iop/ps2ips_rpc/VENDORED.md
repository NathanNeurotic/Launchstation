# PS2IP socket RPC server

Source: [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk), pinned at
[`329e2bb43287e16af16c4dde1c614e2662c24468`](https://github.com/ps2dev/ps2sdk/tree/329e2bb43287e16af16c4dde1c614e2662c24468).

- `ps2ips.c`, `imports.lst`, `irx_imports.h`: `iop/tcpip/tcpips/src/`.
- `ps2ip_rpc.h`: `common/include/ps2ip_rpc.h`, including the fixed-width
  select RPC layout introduced upstream in May 2026.
- `PS2SDK_LICENSE.txt`: upstream `LICENSE` (Academic Free License 2.0).

Original source notices and copyrights are retained.

The PS2BUILD SDK's `ps2ips` target supplies the EE client library. It is not an
IRX artifact. Launchstation builds this server as the separate IOP target
`ps2ips_rpc` and embeds it with `ps2ips_rpc:ps2ips`, retaining the runtime symbols
`ps2ips_irx` / `size_ps2ips_irx`. The standard PS2BUILD target compiles the
corrected [EE client](../../ee_libs/tcpips/VENDORED.md) directly; `ps2ips`
supplies its libc adapter.
The runtime loads this server only for Internet clients, after their IP stack.

Local changes to the upstream server:

1. DNS replies copy the resolved IPv4 bytes rather than the address of the IOP
   pointer; malformed or missing address lists return a lookup failure.
2. A failed `StartThread` deletes the unstarted thread and reports
   `MODULE_NO_RESIDENT_END`, preventing an EE bind to an absent RPC server.
3. `imports.lst` includes `DeleteThread` for that cleanup.
4. UDP receive initializes the sender-address capacity and postpones writing
   the returned address until its overlapping request fields (`ee_addr` and
   `intr_data`) have been consumed. This preserves the destination pointers
   for the received packet and its DMA/reassembly metadata.
5. The shared socket transfer buffer reserves 64 bytes of headroom, covering
   a full 1024-byte payload at every supported alignment.
6. `accept`, `getsockname` and `getpeername` initialize the address length
   and buffer before calling lwIP, and `setsockopt` rejects option lengths
   outside its 128-byte buffer.
7. TCP/UDP reads with no payload return cleared receive metadata, preventing
   the EE callback from replaying an earlier read's head/tail into TLS memory.
   Metadata DMA completes before the RPC returns, including on errors and EOF.
8. The shared wire header uses libcglue socket/configuration types on EE and
   legacy `tcpip.h` only on IOP. EE completes the IPv4 address tag and uses the
   same default TCP/UDP connection counts (5/4) as the IOP stack for select's
   bitmap, while respecting an already-active configuration.
9. The DNS IPv4 field is named `address`. `h_addr` is a lwIP macro expanding to
   `h_addr_list[0]`; using that name declared a zero-length array on IOP and
   omitted the address from the RPC reply. Both peers now agree on a 12-byte
   reply, including the four IPv4 bytes.
10. The server includes the owned `ps2ip_rpc.h` with quotes. PS2BUILD searches
   SDK/common before target include directories; an angle-bracket include
   selected the SDK's unmodified header and broke the DNS field references.

Items 7-10 come from saildot4k/Launchstation (c7884a4, b169f80).
The 2026-10-27 build correction was checked by saildot4k with CI's SDK core
`v2026.10.13.1` and toolchain `v2026.09.27`: real EE client and IOP server
objects compile, and both compilers agree on 12-byte DNS replies, 36-byte
select packets and 144-byte receive metadata. The host regression uses
libcglue's split type contract and lwIP's real `h_addr` macro spelling. The
include-order follow-up reproduces CI's SDK-first search order and preprocesses
the production server against a conflicting SDK header in the host regression.

`ps2.yaml` provides an isolated PS2BUILD compile check. The root `ps2.yaml`
builds the same source with DNS enabled.
