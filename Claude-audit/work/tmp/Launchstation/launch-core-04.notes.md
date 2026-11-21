Done: all 5 files read full (config.c, config_advanced.c, config_network.c, config_screen.c, config_startup.c). Verification done. Next: write findings JSON.

Findings planned:
1 classic config menu superseded by dash SETTINGS (dash_main.c:456-459, parity doc) -> delete ~1960 lines, 23,148 B .text MEASURED
2 dead gc'd funcs: configFormatSavePathValue, configDefaultSaveTargetPath, saveConfig, configSaveNetworkToPath
3 Hide_Paths/Show_Titles no consumer
4 Hide HDD option + table duplicated startup/advanced
5 config_advanced dup branches 123-149/155-181
6 swapKeys tooltip pairs x18
7 configSaveTargetPrompt default_sel dead + double format; save_path select dup
8 screen reset defaults x3 + initConfig
9 mc0/mc1->mc rewrite x4 vs mcAlias dash_settings.c:2098
10 preloadCNFFd stream fallback (INFERRED)
11 loadConfig small leftovers var_cnt/tst/empty branches/getExplicitMcPort overlap
