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

This standalone target is integration groundwork. It is not yet embedded or
loaded by the application. Filesystem path registration, EE bus arbitration,
shutdown/reset ordering and Files/Devices integration remain acceptance work.
Do not manually load it over an existing DEV9 driver: the register gate detects
powered/busy hardware but cannot establish ownership of a resident idle driver.
