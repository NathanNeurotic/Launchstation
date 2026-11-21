Done: all 6 files read fully. All candidate checks done. Writing findings:
F1 update_psu file/stage paths test-only (self_update.c uses only Locate + SidecarMem; gc discards 3 fns MEASURED)
F2 extract_sidecar two-pass walk
F3 SMB3 kind 3 does not round trip (dash_settings.c:535, smb_cnf.c:143)
F4 splitSmbUrl/splitNfsUrl host:port dup
F5 vkey identical char getters (+ dash_widgets ternaries)
F6 vkey row-search duplicated layout vs editor
F7 SmbShare.error write-only
F8 smbSharesConnect retry_failed always 1
F9 small smb clutter: SMB_CNF_MAX_SIZE twice, stale 445 comment, h:140 comment, dead store cnf:602
