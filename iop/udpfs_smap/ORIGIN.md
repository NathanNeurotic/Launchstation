# UDPFS SMAP receive driver

Source: [Neutrino](https://github.com/ps2max32/neutrino/tree/06188cce7cd07d2a158cb4b2646c5719fc16a565/iop/smap),
pinned to `06188cce7cd07d2a158cb4b2646c5719fc16a565`.
`include/mprintf.h` comes from that revision's `iop/common/mprintf.h`.
The upstream AFL-3.0 license is retained in `LICENSE`; preserve file-level notices.

Local modifications:

- `src/xfer.c`: read the final padded FIFO word and store only valid bytes.
  Upstream's halfword-only tail leaves the final byte of odd-sized reads stale.
  This is the same causal repair as RiptOPL commit
  `582f562f` for Open-PS2-Loader issue #1010. No transmit, PHY, discovery,
  timing, retry, or protocol changes are imported from RiptOPL.
- `src/exports.tab`: define `_retonly` after the unchanged export table for
  PS2SDK iopfixup compatibility. Export name/version/ordinal order stay intact.
- PS2SDK Makefile and ps2build target for reproducible source builds.

The old upstream wLaunchELF_R3Z / Launchstation precompiled SMAP is 8,216 bytes,
SHA-256 `4b81d9bf0c176d3291258eb97f6188a2bb935a69ae4cba13546b51d603ee1a09`.
Its MIPS disassembly at `.text+0x10bc` tests for two trailing bytes, stores
one halfword and returns at `0x10d8`; it has no one-byte tail. Retain that
binary only as a historical reference; neither build route should embed it.

`tests/test_udpfs_smap_fifo_tail.py` exercises the production function with a
simulated FIFO across lengths, offsets and DMA sizes, checking destination
bounds and padded FIFO reads. It also requires the old algorithm to fail.
Build and host-model evidence do not establish physical-console reliability.
Acceptance requires UDPFS-to-card and UDPFS-to-USB copies of odd/even files,
byte/hash comparison after readback, a server restart, and actual Neutrino launch.
