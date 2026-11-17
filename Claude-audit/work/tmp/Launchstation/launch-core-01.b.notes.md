Unit launch-core-01.b (defects lens) notes

Done: all 14 files read in full (libhdd.h, libhdd.c, SifIopRebootBuffer.c, patch_romdrv_set_rom1_info.c, ps2ipc.c, wle_igr.c, cdvd_macro.h, cloud_google_roots.h, config_private.h, dash.h, dash_autotest.h, dash_font_data.h, dash_internal.h 1-1195, draw_private.h)

Kept candidates (checked):
- igr/wle_igr.c:122-134,151-154 | driver set reloaded per retry; ps2hdd_osd hdd.c:359-378 stays resident; ata_bd ps2atad.c:331-342,374-381 cbs then NO_RESIDENT_END; sceAtaInit caches init (1388-1396) so reload gives no benefit | rank 4
- include/dash_internal.h:39,690-702 | DashApp 828 B x 512 = 423,936 B malloc'd permanently (dash_lib.c:496) + another at each scan (dash_lib.c:889); pointer stability comment dash_lib.c:764-765; views rebuilt at dash_main.c:718-719 | rank 4, fix M
- ee_libs/hdd/libhdd.c:262-293 | walk only feeds hddFree (read only by uncalled hddGetInfo); triggered by hddCheckPresent from dash_hdd.c:152 and after make/format 454/130 | rank 2

Dropped: libhdd fd0 check, sizesMB[-1] reads, IOPBTCONF memcpy (trusted embedded image), ps2ipc length handling (server bounds len at ps2ips.c:69,471,489), send without server check, cloud roots header (single includer cloud_http.c).
