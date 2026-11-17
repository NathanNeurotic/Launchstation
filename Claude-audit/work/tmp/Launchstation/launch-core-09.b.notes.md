# launch-core-09.b notes (defects lens)

Done: all 4 files read full (dash_font.c 1-957, dash_ftp.c 1-134, dash_gfx.c 1-938, dash_hdd.c 1-1342). Writing findings.

Candidates (verified):
- src/dash_hdd.c:158-161 | HDIOC_TOTALSECTOR int >2^31 for 2TB -> size_mb 0 -> create blocked (hdd_fio.c:1055,1087 int rv=u32 totalLBA; libhdd.c:269 uses u32 cast)
- src/dash_hdd.c:126,716 | HddJob.a 40 bytes truncates 64-char HDL title; IOP strcmp full name (hdl_info/hdd.c:178) -> rename fails >39 chars
- src/dash_hdd.c:151-156 | hddCheckPresent/Formatted repeat HDIOC_STATUS; first call hddUpdateInfo walks all APA headers (libhdd.c:75-102,262-293); cache 3 (hdd.c:218)
- src/dash_hdd.c:162 | 1400*180 B = 252000 B per scan, held while screen open
- src/dash_font.c:80-101 | ELISA atlas 512x1024 but 108*8=864 rows used: 80 KB RAM, ~64 KB VRAM; never freed (dashFontShutdown no caller)
- src/dash_font.c:688-715,798-807 | quadratic measure per frame (dash_browser.c:3363 per row, dash_ui.c:1218)
- src/dash_ftp.c:117-119 | 100 ms sleep + vsync per loop; taps lost (pad.c:62 edge detect)
Dropped: dash_gfx.c (no defects), textWrap escape split (rare), PDIOC_ZONEFREE error cast (display only), stale free_mb in injectMatching (HDD refuses)
