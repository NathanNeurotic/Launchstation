# launch-core-01 notes
Done: all 14 files read fully (libhdd.h, libhdd.c, SifIopRebootBuffer.c, patch_romdrv_set_rom1_info.c, ps2ipc.c, wle_igr.c, cdvd_macro.h, cloud_google_roots.h, config_private.h, dash.h, dash_autotest.h, dash_font_data.h, dash_internal.h, draw_private.h).
Candidates (all checked):
- dash_internal.h/dash.h/config_private.h | 29 prototyped functions with no caller, all gc-discarded 5108 B | done
- libhdd.c:135-569 | 4 unused funcs + hddUpdateInfo state only for hddGetInfo + header macros | done
- libhdd.c DEBUG/PFS_FRAGMENT/_OMIT dead preprocessor | done
- cdvd_macro.h LIBCDVD_LEGACY never defined; readCD.h snippet dead; empty macros | done
- config_private.h CUSTOM_COLORS identical branches | done
- dash_autotest.h DASH_AUTOTEST hook never built | done
- SifIopRebootBuffer.c Encrypted unused, F_ guards always on, extinfo unused | done
- wle_igr.c:98-153 HDD modules reloaded per retry | done
- dash_internal.h small unused macros/enum, duplicate prototypes, stray comments | done
- dash.h:11-13 stale DashRun comment | done
- ps2ipc.c leads refuted: functions in final ELF, installed via libps2ips ps2ipc_ps2sdk.o
