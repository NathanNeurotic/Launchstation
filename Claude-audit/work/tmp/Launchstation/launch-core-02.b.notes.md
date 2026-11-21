# launch-core-02.b notes (lens defects)

Done (read_full): all 17 files.

Candidates (all checked, all dropped):
- launchelf.h:548 PathPad 30 KB bss: live feature (dash_browser.c, dash_files.c, config.c) -> not a defect
- launchelf.h:279-340 SETTING ~27.5 KB: one heap copy + snap_classic in dash_settings.c:959 -> other file, not a defect here
- FILEINFO x MAX_ENTRY arrays: allocations in filer_actions.c/filer_browser.c -> other units
- readCD.h: only under LIBCDVD_LEGACY branch (filer.c:656-657) -> dead, other lens
- loader.c: _stack 0x917a8 ok (evidence loader-output-sections.tsv); fileXio always loaded before loader (no reset path without FILEXIO; init.c:2084 reloads it); argv in loader bss; segment overlap needs >15 MB payload -> dropped
- chkesr.c:42 stack DMA buffer: cdvdfsv copies unaligned head/tail (RiptOPL cdvdfsv/ncmd.c:137-151 shows design) -> dropped
- ftp_protocol.h sizes 64/256 ok, EE/IOP buffers aligned(64) (ftp_session.c:10, FtpControl.c:12,101)
- wle_igr_config.h fields bounded at mmce_igr.c:131-133
- SMB_Server_User/Pass bounded (config.c:1050-1053)
- if_conf/ip/netmask/gw sizes consistent (main_startup.c:133-137)
- PM_flag bound at filer_copy.c:797
- Out of unit (not reported): main_startup.c:152-159 strncat into SystemCnf_* overflows on repeated keys; elf.c:679 MBR payload at 0x01000000 over heap

Result: zero findings.
