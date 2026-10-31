/*
 * embed_blobs.S - binary payloads embedded by the PS2BUILD build (ps2.yaml).
 *
 * PS2BUILD's embed_irx: handles IOP targets and SDK driver packages. The
 * payloads below are plain files (or slices of files) that have no package or
 * target to reference, so they are pulled in with .incbin, producing the
 * same style of symbols that bin2s used to generate:
 *
 *     <name>       - first byte of the payload
 *     size_<name>  - 32-bit payload size
 *
 * Paths are resolved through the assembler include path; ps2.yaml passes the
 * project root with -Wa,-I so these stay relative to the repository.
 */

#define EMBED_FILE(sym, file)            \
	.section .rodata;                    \
	.align 4;                            \
	.global sym;                         \
	.global size_##sym;                  \
sym:                                     \
	.incbin file;                        \
sym##_end:                               \
	.align 2;                            \
size_##sym:                              \
	.word sym##_end - sym

#define EMBED_SLICE(sym, file, off, len) \
	.section .rodata;                    \
	.align 4;                            \
	.global sym;                         \
	.global size_##sym;                  \
sym:                                     \
	.incbin file, off, len;              \
sym##_end:                               \
	.align 2;                            \
size_##sym:                              \
	.word sym##_end - sym

#define EMBED_TAIL(sym, file, off)       \
	.section .rodata;                    \
	.align 4;                            \
	.global sym;                         \
	.global size_##sym;                  \
sym:                                     \
	.incbin file, off;                   \
sym##_end:                               \
	.align 2;                            \
size_##sym:                              \
	.word sym##_end - sym

/* Exploit installer payloads: only the first 128 bytes (the XLF header) are embedded. */
EMBED_SLICE(exploit_system_xlf_header, "exploits/SYSTEM.XLF", 0, 128)
EMBED_SLICE(exploit_xsystem_xlf_header, "exploits/XSYSTEM.XLF", 0, 128)
EMBED_SLICE(exploit_dtl_system_xlf_header, "exploits/DTL-SYSTEM.XLF", 0, 128)
EMBED_TAIL(exploit_xlf_body, "exploits/SYSTEM.XLF", 128)
EMBED_FILE(exploit_osdsys_kernel, "exploits/OSDSYS.KERNEL")
EMBED_FILE(exploit_osd110_kernel, "exploits/OSD110.KERNEL")
EMBED_FILE(exploit_jpn_sys, "exploits/JPN.sys")
EMBED_FILE(exploit_usa_sys, "exploits/USA.sys")
EMBED_FILE(exploit_eur_sys, "exploits/EUR.sys")
EMBED_FILE(exploit_chn_sys, "exploits/CHN.sys")
EMBED_FILE(exploit_list_icn, "exploits/list.icn")
EMBED_FILE(exploit_ioprp_img, "exploits/IOPRP_LTS.IMG")

/* UDPFS SMAP is source-built by ps2.yaml. The unchanged ministack/ioman
 * prebuilt modules retain the existing export-table ABI. */
EMBED_FILE(udpfs_ministack_irx, "iop/__precompiled/udpfs_ministack.irx")
EMBED_FILE(udpfs_ioman_irx, "iop/__precompiled/udpfs_ioman.irx")

#ifdef SMB
/* smb: (SMB2/SMB3): smb2man.irx from libsmb2's wLaunchELF branch, prebuilt;
 * see iop/__precompiled/SMB2MAN.md. smbman (smb1:) is source-built. */
EMBED_FILE(smb2man_irx, "iop/__precompiled/smb2man.irx")
#endif

/* MrRexD network-compatibility fallback: ps2sdk v1.0 DEV9/PS2IP. */
EMBED_FILE(ps2dev9_v10_irx, "iop/__v10/ps2dev9.irx")
EMBED_FILE(ps2ip_v10_irx, "iop/__v10/ps2ip.irx")

/* Dashboard UI sounds (Sony ADP, played through the SDK audio driver via audsrv-compat). */
EMBED_FILE(sfx_cursor_adp, "res/sfx/cursor.adp")
EMBED_FILE(sfx_confirm_adp, "res/sfx/confirm.adp")
EMBED_FILE(sfx_cancel_adp, "res/sfx/cancel.adp")
EMBED_FILE(sfx_transition_adp, "res/sfx/transition.adp")
EMBED_FILE(sfx_message_adp, "res/sfx/message.adp")
EMBED_FILE(sfx_boot_adp, "res/sfx/boot.adp")
EMBED_FILE(sfx_connect_adp, "res/sfx/bd_connect.adp")
EMBED_FILE(sfx_disconnect_adp, "res/sfx/bd_disconnect.adp")

/* Optional "PS2" sound theme: original synthesized effects, see scripts/gen_sfx.py. */
EMBED_FILE(sfx2_cursor_adp, "res/sfx_ps2/cursor.adp")
EMBED_FILE(sfx2_confirm_adp, "res/sfx_ps2/confirm.adp")
EMBED_FILE(sfx2_cancel_adp, "res/sfx_ps2/cancel.adp")
EMBED_FILE(sfx2_transition_adp, "res/sfx_ps2/transition.adp")
EMBED_FILE(sfx2_message_adp, "res/sfx_ps2/message.adp")
EMBED_FILE(sfx2_boot_adp, "res/sfx_ps2/boot.adp")
EMBED_FILE(sfx2_bd_connect_adp, "res/sfx_ps2/bd_connect.adp")
EMBED_FILE(sfx2_bd_disconnect_adp, "res/sfx_ps2/bd_disconnect.adp")

/* Background music is an optional BGM.ADP beside the ELF, not embedded. */
