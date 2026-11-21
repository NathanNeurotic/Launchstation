# launch-core-22 notes

Done so far: src/init.c lines 1-3205 (all). include/kelf_signer.h 1-21 read. Next: verify candidates, write findings.

Candidates (path:line | what | to check)
- src/init.c:51-53,1224-1227 | IOPTRAP conditional, no build defines | grep ps2.yaml, ioptrap_irx embed
- src/init.c:56-58,3085-3091 | UDPTTY conditional | grep
- src/init.c:73-75,3079-3083 | POWERPC_UART conditional | grep
- src/init.c:1244-1251 | SIO_DEBUG conditional | grep
- src/init.c:823-825,2061-2064,3052 | NO_IOP_RESET conditional | grep
- src/init.c:68-71,1236-1242,1259-1265,1313-1317,1907-1913 | HOMEBREW_SIO2MAN always defined, #else dead | ps2.yaml:623 + configure_ds34.py
- src/init.c:83-85,336-338,418-431,720-722,1794-1804,1864-1870,1885-1892,1931-1936,2095-2099,2403-2409,2443-2448,2559-2561 | EXFAT always defined, non-EXFAT branches dead | ps2.yaml:629
- src/init.c:386,748-802,93,215,549 | load_ps2atad_stack marked unused, have_ps2atad never set elsewhere | grep callers; ps2atad embed in ps2.yaml
- src/init.c:1255,1258,1369-1370,3095 | commented-out code | trivial
- src/init.c:1831-1871 | emberUsbProbePath: output only used by DPRINTF at 2065; validation duplicates emberUsbSourceIsUsb | check DPRINTF define, callers
- src/init.c:1703-1706 | pathUsesUsbMass redundant mass<digit> test under EXFAT | trivial
- src/init.c:1806-1809 | USB_mass_loaded only 0/1, else branch | grep USB_mass_loaded assignments
- src/init.c:848-860 vs 864-874 | repeated post-reset re-init sequence in prepareExploitSignerIop | small
- src/init.c:913,957,1016 | #if defined(ETH)&&defined(UDPFS) nested inside ETH/UDPFS | trivial
- src/init.c:336-393 | forward prototype block, some duplicate init.h | check init.h
- src/init.c:1477-1519 | loadExternalFile static: used at 2940 (startKbd) -> used, drop
- src/init.c:752-769 vs 687-705 | duplicate hddarg/pfsarg (lead dup block 726-743==782-797) | part of load_ps2atad_stack dead finding
- src/init.c:2329-2347 | switchPsxHddDriverStack has no side effects (only returns/DPRINTF) | callers 2667, 2719
- src/init.c:2223-2231 | rebootIopAndReloadCoreStack(Silent) one-line wrappers | grep callers
- src/init.c:2500-2504 | pauseAfterAtaBlockDriverLoad wraps sleep(1), one caller | trivial
- src/init.c:2571 | ret = Hdl_Info_BindRpc() unused store | trivial
- src/init.c:2766-2768 | stale RA comment + second getIpConfig | trivial
- include/kelf_signer.h | KELF_SIGN_* macros and prototypes used? | grep
