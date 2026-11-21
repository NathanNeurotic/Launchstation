# launch-core-18 notes (structure lens)

## Done
- src/filer_actions.c lines 1-2259 read (second attempt re-read everything)

## Candidates
- 1166-1170 TMANIP_MORON | grep
- 480-538 To/FromLocalTime dup
- 540-565 vs 578-596 normalize dup
- 981-1070, 1243-1261, 1430-1474 classic UIs if filerTimestampUi always set | grep dash
- 680-696, 877-895, 910-935 helpers only for classic editor
- 1304-1307 vs 1347-1350 tuna re-stamp twice
- 1352-1390 == 1848-1885 apply loop dup
- 1840-1845 dup of sasFolderTimestamp 1729-1738
- 1502-1505 wrapper; 1541-1546 toupper
- 1627-1631 prefix list
- 1683-1696 vs 649-659
- 2017-2024 vmc branch identical to else? (rmdir: else rebuilds dir w/o slash); 2040-2043 vmc == else identical
- 2242-2253 host/udpfs branch identical to else; 2218-2222 vmc identical to else
- 9 IOCTL_RENAME used 2069; 10 used 2058
- 1103 filerDateEditorFieldCount callers
- 1393 time_manip_custom only internal caller -> make static
- 1917-1936 make_title_cfg callers
