launch-core-26 notes
Done: all 8 files read fully. Checks done; writing findings.
Final findings: rd/wr dup x5; zipCrc vs zlib crc32 (crc32.o linked via libpng); mcSaveFormatExt/Name dead; nextOf==chainNext + FatLocate dup of fatGet; wrappers ReadChain/WalkDir/SameSave; ps1 save check x3; RAW branch dup of takeBlocks; McView version/cardtype/cardflags unread (has_ecc,title_break test-only); mcPsvSign/ps1VmpSign could be static.
Dropped: unused-export sweep (all others used), psu_functions.c vs mcCardSaveToPsu (different contexts), header misplaced comment (cosmetic).
