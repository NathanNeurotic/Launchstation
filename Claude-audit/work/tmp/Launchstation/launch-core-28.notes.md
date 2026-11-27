Done: all 13 files read fully (mmce_save_versions.c, neutrino_gameid.h/.c, neutrino_iso.c, neutrino_smb.h/.c, pad.c, pad_ds34.c, pc_game_receiver.c, popstarter.c, popstarter_elf.c, popstarter_gameid.c, popstarter_internal.h)

Candidates:
- pad.c vs pad_ds34.c | simPadKB/readpad/readpad_noRepeat/waitPadReady/waitAnyPadReady/setupPad copies | check configure_ds34.py, DS34 define
- pad.c:151-276 + ds34:253-378 simPadKB switch -> table | count
- pad.c:351-370, ds34:415-428 stateString written never read | grep padStateInt2String
- waitAnyPadReady callers | grep
- pad_ds34.c:149-152 msTime wrapper | grep msTime
- pad.c:284-325 DASH_AUTOTEST | grep dash_autotest.h, tests
- pad_ds34.c readpad_no_KB stick alternation bug fixed in pad.c only
- neutrino_gameid.c ISO9660 SYSTEM.CNF reader vs popstarter readVcdSystemCnf/readPs1SystemCnf | find file
- neutrino_iso.c fileExists vs wleExists; setMessage
- neutrino_iso.c cwd derivation duplicated 58-64 / 242-250
- neutrino_smb.c keys[] twice 87/145
- mmce_save_versions svJoin vs other join helpers; svNewest/svPrune scan dup
- pc_game_receiver fileSize return unused
- popstarter_internal.h unused macros/prototypes | grep
- popstarter_elf.c path-separator strip repeated 3x (124,142,154)
- getFinalNameComponent vs other basename helpers
