# PS2IP socket RPC client

`ps2ipc.c` comes from `ee/rpc/tcpips/src/ps2ipc.c` in
[ps2dev/ps2sdk at 329e2bb43287e16af16c4dde1c614e2662c24468](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/tcpips/src/ps2ipc.c).
Original notices are retained; see the
[Academic Free License 2.0](../../iop/ps2ips_rpc/PS2SDK_LICENSE.txt).

The standard PS2BUILD target compiles this client directly. The SDK's
`libps2ips` still supplies its libc socket adapter; its original client object
is not needed because the local client supplies those symbols. The client
includes the embedded server's reviewed wire header, with DNS enabled.

Local fixes:

- Size the cache-aligned receive metadata buffer with `sizeof(rests_pkt)`.
  The original 128-byte allocation is smaller than the 144-byte PS2 wire
  packet, allowing every receive metadata DMA to write past its allocation.
- Ignore receive callbacks for errors, nonblocking retries and EOF. An older
  server can leave the previous successful read's metadata in place; copying
  it again can overwrite bytes TLS has decrypted or memory it has freed.
- Use EE libcglue's socket/configuration types through the shared wire header,
  rather than including IOP's conflicting legacy `tcpip.h`. DNS uses a private
  standard host-entry view because libcglue only forward-declares `hostent`.
  IPv4 zero/cache operations do not depend on legacy lwIP helper macros.
- Read the DNS packet's `address` field, whose name cannot expand through
  lwIP's `h_addr` macro. Both owned peers must use the same corrected header.

The matching [IOP server](../../iop/ps2ips_rpc/VENDORED.md) also clears metadata
when there is no payload and waits for metadata DMA before completing the RPC.
`tests/test_ps2ips_rpc.py` runs the actual client receive functions and server
transfer functions together, including deferred DMA completion and retry
sequences. It also compiles the complete EE client against the split SDK header
contract, checks default/configured select bitmap sizes, and rejects a DNS
packet missing its four IPv4 bytes. These host checks do not emulate EE cache
hardware.
