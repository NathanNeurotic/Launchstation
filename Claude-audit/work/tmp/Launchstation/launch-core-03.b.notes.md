# launch-core-03.b notes (lens defects)

## Done
- src/cloud_google.c 1-1635 (read fully, attempt 1; re-read fully in attempt 2)
- include/cloud_http.h 1-56 (read fully, attempt 2)
- src/cloud_http.c 1-1312 (read fully, attempt 2)
- candidates verified; writing findings JSON next

## Kept
1. cloud_google.c:1543-1549 | card images bypass UPLOADED.TXT ledger -> 1 TLS session per old snapshot per sync | verified: prune ignores non-WLE (mmce_backup.c:132), keep default 10 (dash_store.c:260), UI thread (dash_tools.c:360-363,676-679), IGR (main.c:198); test asserts psu-only (test_cloud_upload_ledger.py:6,51-53)
2. cloud_google.c:1626 | token fetched once, no refresh/401 handling; long sync aborts | grep 401/refresh_access_token: only 4 call sites at start of each op
3. cloud_google.c:1043-1076 | .mcd recovery copies download to stage (mcCardBackupNoEcc: 24 MB read + 8 MB write, open/close per 64 KB) yet digest only covers in-flight bytes (cloud_http.c:863) | test_cloud_recovery.py:58-59 asserts stage flow
4. cloud_google.c:890-895,913-914 | page token >240 rejected though buffer 255; truncation silent | token length unverified -> Low

## Dropped
- stack depth (main thread ~128 KB, realistic depth ~5), IsLinked per call (only on setting change / menu loop), fileXioRename raw path (mmce_backup also uses raw root), json_string backslash, error text not set (callers fall back), cloudHttpRequest NULL ca_path (never NULL), EOF w/o close_notify (digests checked by callers), psa_crypto_init (speculative), per-request handshake as own finding (fix adds code; folded into 1)
