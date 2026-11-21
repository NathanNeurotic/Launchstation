# launch-core-09 notes

## Done
- src/dash_font.c 1-957 (read full, re-read in this attempt)
- src/dash_ftp.c 1-134 (read full)
- src/dash_gfx.c 1-938 (read full)
- src/dash_hdd.c 1-1342 (read full)

## Verified findings (writing JSON)
- F1 dashGfxShutdown/dashFontShutdown dead (gc-discarded 180+120 B, MEASURED raw/link)
- F2 dashTextWrapScrollTimed dead (gc 804 B) + textWrap draw param
- F3 dash_font small: fwd decl 316, glyphAdv dup branches, unreachable c<FONT_FIRST
- F4 dashSinf/dashCosf forwarders
- F5 elisaGlyphIndex duplicates draw_text.c:266-291
- F6 dash_hdd hm.scanning unused, hm.changed=1 at 902 redundant

## Dropped
- dash_ftp ETH stub (repo-wide 129 #ifdef ETH; not unit-local)
- gfx redundant batch-end calls (needed for ordering when dashRect early-returns)
- editorForHeader wrapper (needed: const char* vs char* signature)
