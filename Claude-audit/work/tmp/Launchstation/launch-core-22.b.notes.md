# launch-core-22.b notes (lens defects)

## Read status
- src/init.c lines 1-3205: DONE
- include/kelf_signer.h lines 1-21: DONE

## Result
- findings/Launchstation/launch-core-22.b.json written, validator OK (6 findings)
- kept: HDL info stale/unbound RPC; exploit signer reboot teardown; poweroff re-arm; ATA sleep(1); getExternalFilePath NULL; loadExternalModule def_OK
- dropped: SMB args[64] (32+32 fits), smb_load_error[4] (NFS=3), fxstat cache line (map shows 64-aligned), ember handoff failure (caller ExecOSD), ret>=0 inconsistency, vmcman reload, ember 2 s wait
