# launch-core-02 notes
Done: all 17 files read fully. Candidate checks done; writing findings JSON.

Confirmed:
- icon chain: loadIcon never called -> TexIcon, icon.c (2055 lines), FOLDER/WARNING (gc evidence: icon.o 0 bytes, TexIcon 80 B bss)
- UDPTTY/POWERPC_UART/IOPTRAP/COMMON_PRINTF debug variants unbuildable (no udptty/ppctty/ioptrap IRX anywhere)
- LIBCDVD_LEGACY never defined: snippets/readCD.h + branches
- reopenPadPorts never called (pad.c + pad_ds34.c) + pad_reopen_active chain
- unused: filerPasteInto, getVirtualKeyboardEditorDisplayChar, CurrTime, mcfreeSpace, HFONTS
- file-local symbols exported via headers (xref relocs-own-object)
- duplicate externs LastDir/elisaFnt/latestDVRPMount
- PS2TIME == ps2time duplicate struct
- cosdgf/sindgf unused
- wle_stricmp/wle_strnlen reimplement newlib (strcasecmp used elsewhere)
- smb2Connect_out_t never written
- loader N/D flags never emitted (elf.c:577, 823)
- WriteFont_C commented block + stale comments
Refuted: loader _libcglue_* / _ps2sdk_fileXio_deinit leads (they override SDK symbols)
