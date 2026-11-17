## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/virtual_keyboard.c:371  function `getVirtualKeyboardEditorDisplayChar`  (only-prototype-or-export-mentions) | also mentioned: include/launchelf.h:703 char getVirtualKeyboardEditorDisplayChar(int layout, int editor_index, int caps);

## Largely identical files (dupes-file-pairs.tsv)
- src/smb_cnf.c repeats itself: 18 cloned normalised lines inside the file (3%)
- src/update_psu.c repeats itself: 13 cloned normalised lines inside the file (4%)

## Duplicate blocks (dupes-blocks.tsv)
- src/smb_cnf.c:194-213  ==  Launchstation/src/smb_cnf.c:246-265  (18 normalised lines)
- src/smb_cnf.c:246-265  ==  Launchstation/src/smb_cnf.c:194-213  (18 normalised lines)
- src/update_psu.c:255-267  ==  Launchstation/src/update_psu.c:274-286  (13 normalised lines)
- src/update_psu.c:274-286  ==  Launchstation/src/update_psu.c:255-267  (13 normalised lines)
