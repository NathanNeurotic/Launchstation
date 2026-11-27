# launch-core-13.b notes (lens defects)

Done (read fully): src/dash_smbd.c 1-83, include/dash_sound.h 1-34, src/dash_sound.c 1-525, src/dash_store.c 1-870, src/dash_theme.c 1-148, src/dash_themes.c 1-1308

Kept:
- src/dash_sound.c:40,465-472,54,147-149 | BGM (284,656 B shipped) and custom SFX EE copies held all run as audsrv ids; freed on IOP reset anyway (shutdown 299-301) so never reused | finding 1
- src/dash_sound.c:407-450,136-158,492-516 | sidecar reads on UI thread (boot dash_main.c:1517-1521, after IOP reset via dashSoundMaintain 1484, misc 356-358, launch fail 676-678) | finding 2

Dropped:
- dash_smbd.c login layout: smbd_login[64] user\0pass (init.c:2783-2819) ok
- dash_store.c text caps: header+scans+theme well under cap; DASHAPPS max ~166 KB < 256 KB limit; DASHBOARD ~9.4 KB < 64 KB
- dashWriteFile O_TRUNC in place: mc has no rename path (psu_functions.c:24-27), fix not simple; dropped
- dash_themes.c u8 field writes: all DashTheme fields u8 (dash_internal.h:215-243) ok
- theme folder IO runs in worker (dash_settings.c:2893-2897) ok
- dashBgmStart retry of dashSoundInit, fallback play loop: only on rare failures, dropped
- keep-IOP handoff shutdown: committed right after (main_actions.c:71-74), ok
