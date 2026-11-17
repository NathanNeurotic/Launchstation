# launch-core-03 notes

## Done
- All three files read in full (attempt 2). Findings file written and validated OK (8 findings).

## Dropped candidates
- mbedtls_ms_time / mbedtls_hardware_poll leads: required by MBEDTLS_PLATFORM_MS_TIME_ALT and MBEDTLS_ENTROPY_HARDWARE_ALT (scripts/build_cloud_tls.sh:37, 66).
- progress total always 0: dashLoadingStatus handles it; not worth a finding.
- store-path helper: net saving too small.
- two chunked decoders: no clear net win.
- MBEDTLS_X509_REMOVE_INFO guard: harmless upstream config guard.
