## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/config.c:1342  function `configDefaultSaveTargetPath`  (only-prototype-or-export-mentions) | also mentioned: include/config_private.h:70 int configDefaultSaveTargetPath(const char *filename, const char *loaded_path, char *out, size_t out_size);
- src/config.c:788  function `saveConfig`  (no-other-mention)
- src/config.c:86  function `configFormatSavePathValue`  (only-prototype-or-export-mentions) | also mentioned: include/config_private.h:54 void configFormatSavePathValue(char *dst, size_t dst_size, const char *path, const char *loaded_path);
- src/config_network.c:70  function `configSaveNetworkToPath`  (only-prototype-or-export-mentions) | also mentioned: include/config_private.h:77 void configSaveNetworkToPath(char *msg, const char *target_path);

## Largely identical files (dupes-file-pairs.tsv)
- src/config_advanced.c is 4% the same as Launchstation/src/config_startup.c (12 cloned normalised lines)
- src/config_advanced.c repeats itself: 27 cloned normalised lines inside the file (10%)
- src/config_screen.c is 9% the same as Launchstation/src/dash_settings.c (28 cloned normalised lines)
- src/config_startup.c is 4% the same as Launchstation/src/config_advanced.c (12 cloned normalised lines)

## Duplicate blocks (dupes-blocks.tsv)
- src/config_advanced.c:123-149  ==  Launchstation/src/config_advanced.c:155-181  (27 normalised lines)
- src/config_advanced.c:155-181  ==  Launchstation/src/config_advanced.c:123-149  (27 normalised lines)
- src/config_advanced.c:27-41  ==  Launchstation/src/config_startup.c:30-44  (12 normalised lines)
- src/config_screen.c:166-179  ==  Launchstation/src/dash_settings.c:2770-2783  (14 normalised lines)
- src/config_screen.c:166-179  ==  Launchstation/src/dash_settings.c:2791-2804  (14 normalised lines)
- src/config_startup.c:30-44  ==  Launchstation/src/config_advanced.c:27-41  (12 normalised lines)
