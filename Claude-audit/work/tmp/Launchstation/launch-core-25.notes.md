Done: all 11 files read. Writing findings JSON.

Findings planned:
1 main_info_screens.c whole superseded by dash_info.c (rank 4, INFERRED reach)
2 makeicon tex_drawChar 10548 B measured -> direct 2x2 write
3 makeicon tex_compresRLE extra malloc/copy + tex_buffer leak
4 mc_card_export five copy/verify loops
5 mc_card_export helper dup (write_exact_at/_new, read_exact_at/fd, source_info_fd/pcsx2_source_info)
6 main_title dead menuTitleFormat/GetClockText (gc-discarded 660+220 B) + dup prototypes dash_internal.h:1161-1167
7 main_title MENU_TITLE_FORMAT token machinery always true
8 main_startup buildBootBlockPath -> snprintf
9 main_startup small redundancies
10 main_modules small (unused param, dead WaitTime stores, wrapper, stale comments)
11 McView cardtype/cardflags/version never read
