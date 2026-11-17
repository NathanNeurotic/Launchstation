# launch-core-15.b notes (lens defects)

Done: all 6 files read fully (dash_widgets 1-1350, dash_worker 1-405, draw 1-402, draw_gs 1-206, draw_text 1-363, editor 1-688). Next: write findings JSON.

Findings:
1. kbInsert max_len = chars excl NUL; callers passing buffer size -> 1-byte overflow: classicKeyboard dash_widgets.c:663-668 (main_exploit_installer.c:962/981 name[36] with 36, reachable via dashExploitInstaller with override installed dash_main.c:1358, dash_exploit.c:159-160); dash_settings.c:690-693 edit[96], 3377-3381 buf[8], 3408-3411 buf[12]. Rank 3.
2. dashPickPath memset resets pk.gen (dash_widgets.c:1099) -> stale job from previous session can match (pickListDone 927). Rank 2, Med.
3. loadFont draw_text.c:79-96 accepts truncated .fnt (no min size, genRead unchecked). Rank 1. Reachable via dash_settings.c:2128.

Dropped: config.c:1552 (override off), worker park machinery (dead, other lens), editor Editor_nRowsWidth unbounded (editor_rules.c:26) but classic TextEditor unreachable from dashboard (MISC routed to dashRunMisc) -> dead-code lens; loadIcon free of static (dead); drawScr 5ms busy wait (classic, minor); ipParse overflow (theoretical).
