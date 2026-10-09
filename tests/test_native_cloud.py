from pathlib import Path

root = Path(__file__).resolve().parents[1]
google = (root / "src/cloud_google.c").read_text(encoding="utf-8")
mc_export = (root / "src/mc_card_export.c").read_text(encoding="utf-8")
mc_header = (root / "include/mc_card_export.h").read_text(encoding="utf-8")
http = (root / "src/cloud_http.c").read_text(encoding="utf-8")
build = (root / "scripts/build_cloud_tls.sh").read_text(encoding="utf-8")
docs = (root / "docs/NATIVE_CLOUD.md").read_text(encoding="utf-8")
yaml = (root / "ps2.yaml").read_text(encoding="utf-8")
init = (root / "src/init.c").read_text(encoding="utf-8")
settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
tools = (root / "src/dash_tools.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")

# Public Google API contract.
assert "https://www.googleapis.com/auth/drive.file" in google
assert '"/device/code"' in google
assert '"/token"' in google
assert "uploadType=resumable" in google
assert "trashed=false" in google
assert "appProperties has { key='wle_sha256' and value='%s' }" in google
assert '\\"appProperties\\":{\\"wle_sha256\\":\\"%s\\"}' in google
assert "hash_open_backup" in google
assert "mbedtls_sha256_update" in google
assert "cloudHttpUploadFd" in google and "cloudHttpUploadFd" in http
assert "cloudHttpUploadFile" not in google and "cloudHttpUploadFile" not in http

# Never publish interrupted/truncated final-named backups.
assert "mcCardValidateNoEcc" in mc_header and "mcCardValidatePcsx2" in mc_header
assert "mcCardValidateNoEcc" in mc_export and "mcCardValidatePcsx2" in mc_export
assert "validate_backup_file" in google
assert "mcCardValidateNoEcc(path) == MC_CARD_EXPORT_OK" in google
assert "mcCardValidatePcsx2(path) == MC_CARD_EXPORT_OK" in google

# Bearer credentials must not follow a resumable Location to an arbitrary host.
assert 'static const char prefix[] = "https://www.googleapis.com/";' in google
assert "is_google_upload_location(location)" in google
assert "unexpected resumable-upload host" in google

# Backup policy is additive only: no Drive delete endpoint/method.
assert "drive.files.delete" not in google
assert '"DELETE"' not in google
assert "genRemove(token_path)" in google and "genRemove(stage_path)" in google  # disconnect removes only local token copies

# TLS must authenticate Google, not silently downgrade verification.
assert "MBEDTLS_SSL_VERIFY_REQUIRED" in http
assert "mbedtls_ssl_set_hostname" in http
assert "mbedtls_ssl_get_verify_result" in http

# Certificate failures should be actionable on real consoles.
assert "MBEDTLS_X509_BADCERT_EXPIRED" in http
assert "MBEDTLS_X509_BADCERT_FUTURE" in http
assert "verify the PS2 date and time" in http
assert "mbedtls_x509_crt_verify_info" in http

# HTTPS framing is bounded and must reject incomplete declared bodies.
assert "HTTPS response headers are too large" in http
assert "HTTPS response body is too large" in http
assert "HTTPS response exceeded the receive limit" in http
assert "Truncated HTTPS response body" in http
assert "content_length > CLOUD_HTTP_BODY_MAX" in http
assert "body_len < declared" in http

# Cloud networking must fail out instead of hanging IGR indefinitely.
assert "#define CLOUD_IO_TIMEOUT_MS 15000" in http
assert "ps2ipc_ioctl(*sock, CLOUD_FIONBIO, &nonblock)" in http
assert "socket_wait(*sock, 1, CLOUD_IO_TIMEOUT_MS)" in http
assert "ps2ipc_socket(" in http and "ps2ipc_connect(" in http
assert "ps2ipc_send(" in http and "ps2ipc_recv(" in http
assert "ps2ipc_select(" in http and "ps2ipc_gethostbyname(" in http
assert "ps2ipc_disconnect(" in http
assert "MBEDTLS_ERR_SSL_WANT_READ" in http and "MBEDTLS_ERR_SSL_WANT_WRITE" in http
assert "wait_tls_io(tls, r)" in http
assert "TLS handshake timed out" in http
assert "HTTPS write timed out" in http
assert "HTTPS read timed out" in http

# PS2-native portability boundary.
assert "unset MBEDTLS_NET_C" in build
assert "MBEDTLS_NO_PLATFORM_ENTROPY" in build
assert "MBEDTLS_ENTROPY_HARDWARE_ALT" in build
assert "-ffunction-sections -fdata-sections" in build
# Do not trust PS2SDK's weak getrandom seed alone: condition independent timing/state.
assert "CloudEntropyMaterial" in http
assert "cpu_ticks()" in http
assert "sceCdReadClock" in http
assert "cloud_entropy_network_ticks" in http
assert "mbedtls_sha256(" in http
assert http.index("ps2_tcp_connect(&tls->sock, host)") < http.index("mbedtls_ctr_drbg_seed")
assert "never present it to TLS as the sole source" in http
assert "ps2_tls_send" in http and "ps2_tls_recv" in http
assert "ps2_tcp_connect" in http
assert "ps2ips" in yaml
assert "WLE_NATIVE_CLOUD" in yaml

# License boundary: native cloud must stay on the permissive Mbed TLS path.
combined = "\n".join((google, http, build, yaml)).lower()
assert "wolfssl" not in combined
assert "-lwolfssl" not in combined
assert "mbedtls" in build.lower()
assert "github.com/mbed-tls/mbedtls" in build.lower()

# IOP lifecycle: native cloud reuses the shared demand-loaded dashboard IP path.
# The SDK routine is idempotent and detects/rebinds after IOP reboot.
assert "ensureDashboardIpNetworkReady()" in google
dashboard_loader = init[init.index("int ensureDashboardIpNetworkReady"):init.index("#endif", init.index("int ensureDashboardIpNetworkReady"))]
assert "ps2ip_init()" in dashboard_loader
assert "loadCloudNetworkModules" not in init
assert "cloud_ee_ps2ip_ready" not in init

# Automatic IGR cloud sync cannot be enabled until the local Drive link is usable.
set_choice = settings[settings.index("static void setChoice("):]
auto_start = set_choice.index("case S_IGR_CLOUD_SYNC")
auto_end = set_choice.index("case S_IGR_AFTER", auto_start)
auto_sync = set_choice[auto_start:auto_end]
assert "cloudGoogleIsLinked()" in auto_sync
assert "dashCfg.igr_cloud_sync = 0;" in auto_sync
assert "Link Google Drive in Tools" in auto_sync

# Refresh-token replacement is power-loss recoverable without relying on rename.
assert '"WLE-CLOUD.TOK.NEW"' in google
assert "Prefer it so interrupted writes" in google
assert "dashWriteFile(s->token_stage_path" in google
assert "dashLoadTextFile(s->token_stage_path" in google
assert "strcmp(verify.refresh_token, token) != 0" in google
assert '"Stage_Complete"' in google
assert "token_stage_complete" in google
assert "recovery copy retained" in google
assert "wleExists(token_path) || wleExists(stage_path)" in google
assert "genRemove(stage_path)" in google

# A damaged/missing client config must never strand a saved local OAuth token.
assert "cloudGoogleHasLocalToken" in google
assert "cloudGoogleHasLocalToken()" in tools
cloud_backup = tools[tools.index("void dashToolsCloudBackup(void)"):]
cleanup = cloud_backup[:cloud_backup.index("static void runTool")]
assert "Clear saved Google token" in cleanup
assert "cloudGoogleDisconnect" in cleanup
assert "dashCfg.igr_cloud_sync = 0;" in cleanup

# Disconnecting the account must also disable persisted automatic IGR sync.
disconnect = tools[tools.index("Disconnect Google Drive?"):tools.index("static void runTool")]
assert "dashCfg.igr_cloud_sync = 0;" in disconnect
assert "dashStoreSaveSettings();" in disconnect
assert "automatic IGR cloud sync was turned off" in disconnect

# User-supplied CA bundles retain the native loader's size guard.
assert "len > 256 * 1024" in http

# Clean-room documentation and no source-level dependency on SD2Cloud.
assert "does not use\nSD2Cloud source code" in docs
for source in (google, http):
    lowered = source.lower()
    assert '#include "sd2cloud' not in lowered
    assert "#include <sd2cloud" not in lowered
    assert "github.com/omrrexd/sd2cloud" not in lowered

print("native cloud contract: OK")
