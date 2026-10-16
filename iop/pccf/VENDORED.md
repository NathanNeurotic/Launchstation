# PC CARD CompactFlash driver origin

Source: https://github.com/mihhru/PS2CF
Pinned commit: `9b6ed3702b59a87512426952e003125a5b4f0e3c`.
Imported from `src/wlaunchelf/iop/pccf`; host tests and the Transcend
CIS fixture came from `src/wlaunchelf/tests`. Credit: mihhru / PS2CF.

The hardware power/reset sequence derives from PS2SDK DEV9 by Marcus R.
Brown (2003), under AFL-2.0; see PS2SDK_LICENSE.txt and preserved file notices.
No FatFs, replacement BDM, firmware, FMCB images, or mymc tooling is vendored.
The module uses the installed PS2SDK BDM 1.1 interface.

Launchstation adaptations: remove the dedicated-session forced power reclaim
from the module entry point; reject a resident DEV9 export library before
any semaphore allocation or MMIO access. Use uintptr_t for pointer alignment
so the host MMIO harness also builds correctly on Windows LLP64. Keep conservative 1000-us PIO settling and the
upstream checked write/flush/read-back path. Host tests exercise the actual
hardware code against MMIO fixtures; they do not establish console acceptance.

The standard and DS34 application definitions embed this driver. The EE
on-demand service reserves the bus during probing and while the module remains
resident, rejects competing DEV9 loads, and uses the Launchstation-specific
`pccfctl:` status/shutdown endpoint. No boot probe or Files/Devices entry calls
the service yet. Filesystem mount/rescan/handle invalidation remains unfinished.
Shutdown is final for an IOP session; it is not a filesystem unmount and does
not release ownership. Only a completed IOP reset releases the EE reservation.

Host ABI fixtures `iomanX.h`, `io_common.h` and `iox_stat.h` were copied from the
installed ps2build v2026.10.13 PS2SDK headers with their AFL-2.0 notices intact.
The license's legacy single-byte section sign is losslessly encoded as UTF-8.
