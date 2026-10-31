# Native Google Drive backup

Launchstation contains an original Google Drive client for uploading the
versioned MMCE backup history directly from a PlayStation 2. It does not use
SD2Cloud source code.

Contents: [Design](#design) · [Requirements](#requirements) ·
[Linking an account](#linking-an-account) · [Menus](#menus) ·
[Files](#files) · [Backup behavior](#backup-behavior) ·
[Remote history browser and recovery](#remote-history-browser-and-recovery) ·
[Security](#security) · [Updates](#update-checks-are-separate) ·
[Maintainer setup](#maintainer-setup-once-for-official-builds) ·
[Source map](#source-map) · [Tests](#tests)

## Design

The implementation uses Google's documented OAuth 2.0 device authorization
flow and Drive API v3. HTTPS is implemented in `src/cloud_http.c` using Mbed
TLS 3.6.7, which is built from upstream source by `scripts/build_cloud_tls.sh`
with the same compiler as the ELF. The cloud code is part of this repository
and follows this repository's license. Mbed TLS remains under its upstream
Apache-2.0 license.

Cloud networking is opt-in and demand-loaded. Normal Launchstation startup does
not start Ethernet or TLS merely because cloud support exists. It uses the
Ethernet (ETH/PS2IP) network stack, never UDPFS; a `udpfs:` backup folder cannot
be the upload source.

Native HTTPS sockets are IPv4, port 443, nonblocking, and use a 15-second
no-progress timeout for TCP connect, TLS handshake, reads and writes. DNS lookup
itself is the synchronous PS2IP resolver call and is not covered by that socket
timeout; DNS failure behavior therefore remains part of the real-hardware
validation matrix. Buffered API responses are limited to 64 KiB; file uploads
and downloads are streamed.

Endpoints used: `oauth2.googleapis.com` (`/device/code`, `/token`),
`www.googleapis.com` (Drive v3 file listing, folder creation, media download and
the resumable upload endpoint) and Google content hosts under
`googleusercontent.com` for download redirects.

## Requirements

- A build with `WLE_NATIVE_CLOUD` and `ETH` defined. `ps2.yaml` (all official
  packages) defines both; a custom build that omits them shows
  "This build does not include native HTTPS support".
- A working Ethernet adapter and IP configuration (`IPCONFIG.DAT`).
- A roughly correct PS2 date and time (see [Security](#security)).
- Either an official build with the embedded client or a custom OAuth client.

## Linking an account

No setup files and no Google Cloud project are needed:

1. Open Tools > Google Drive backups and choose **Link Google Drive**.
2. Scan the QR code with a phone, or open the displayed address (Google
   provides it; normally `https://www.google.com/device`) on any device, and
   enter the displayed code.
3. Sign in and choose Allow. The PS2 checks with Google automatically (at
   Google's requested interval, slowing down by 5 seconds when asked) and shows
   when the account is linked. Back cancels; an unused code expires after
   Google's time limit (30 minutes is assumed if Google omits it). Transient
   network errors are retried a few times; an explicit Google answer such as
   "access denied" ends the attempt.

Official builds embed Launchstation's own OAuth client (type **TVs and
Limited Input devices**) and the Google Trust Services root certificates
(GTS Root R1–R4, `include/cloud_google_roots.h`, valid until 2036-06-22).
Google never asks the user for payment details. The requested scope is
`https://www.googleapis.com/auth/drive.file`: Launchstation can see only the
files it creates, not the rest of the Drive.

## Menus

**Tools > Google Drive backups** (the same actions are reachable from
Settings > Storage & devices > MMCE backups > Google account):

| Item | Behavior |
| --- | --- |
| Back up cards & upload | Creates verified backups of changed MMCE cards (both units are checked), then uploads the local history. Asks for a backup folder if none is saved. |
| Link / Relink Google Drive | Starts the device flow above. |
| Upload existing backups | Uploads history from the saved backup folder without reading live MMCE cards. Requires a linked account and a saved folder; the IGR helper is not required. |
| Browse cloud history... | Read-only paged view of the app-created folders and snapshots; see below. |
| Backup folder... | Chooses the parent of `WLE-MMCE-Backups` and saves it immediately, like the destination picker used by Sync all cards. Cancel or selecting the current folder leaves settings unchanged. Changing an existing folder asks for confirmation and reminds you to reinstall the IGR helper. |
| Disconnect Google Drive / Clear saved Google token | Deletes both local token files and turns off automatic IGR cloud sync. It does not contact Google and does not delete Drive files; revoke the grant at Google Account > Third-party access if desired. |
| Advanced setup... | Custom OAuth client and CA bundle. |

**Backup folder** in Settings selects the parent folder containing
`WLE-MMCE-Backups`; it does not move existing history. Save the setting before
leaving Settings. If an OPL IGR helper is already installed, reinstall it after
changing the folder because the helper embeds its destination. The card status
**Locally backed up** describes only verified local backup history; it does not
claim that Google Drive has been checked.

**Automatic sync** (Settings > Storage & devices > MMCE backups) has three
modes: Off, Local backup, and Local + Google Drive. It is off by default, runs
only after OPL returns through the WLE-IGR helper, and the Drive step runs
after the local backup pass. "Local + Google Drive" cannot be selected until an
account is linked.

## Files

Launchstation keeps these beside `DASHBOARD.CNF` (the dashboard settings folder,
for example `mc0:/SYS-CONF/`):

- `WLE-CLOUD.TOK` — generated by Launchstation after account linking. This
  contains the user's refresh token as readable text and must not be shared.
- `WLE-CLOUD.TOK.NEW` — temporary verified recovery copy used only while
  replacing the refresh token. A completion marker (`Stage_Complete = 1`) is
  written last, so a partially written staging file is ignored and the intact
  primary token is retained. After a power loss a complete `.NEW` file is
  preferred on the next start. Disconnect removes both token files.

Optional advanced overrides (Google Drive backups > Advanced setup):

- `WLE-CLOUD.CNF` — a custom OAuth client ID **and** secret
  (`Google_Client_ID`, `Google_Client_Secret`). When a custom ID is present it
  replaces the built-in client as a pair; the built-in secret is never sent
  with a custom ID. **Use built-in Launchstation client** removes it. Changing
  the client disconnects the account, because Google refresh tokens belong to
  the client that issued them, and turns off automatic Drive sync.
  `res/WLE-CLOUD.CNF.example` is a template. Google's device flow needs the
  secret; the editor does not forbid leaving it empty, but the menu marks it
  "Required by Google".
- `WLE-CLOUD-CA.PEM` — custom trusted roots (a PEM file with at least one
  certificate, at most 256 KiB). When installed it replaces the built-in Google
  roots; otherwise the built-in roots are used.

The update check uses a different trust file, `WLE-UPDATE-CA.PEM`, and never
reads the Google CA override.

## Backup behavior

Cloud sync uploads the existing versioned `WLE-MMCE-Backups` tree into a folder
of the same name in the root of the Drive, mirroring subfolders (at most 16
levels). Only `WLE-*.mcd`, `WLE-*.ps2` and `WLE-*.psu` files are considered, so
in-progress `.PART` files are never uploaded. Before a file is uploaded,
Launchstation validates its PS2 memory-card header/geometry and exact expected
file size; interrupted or truncated final-named backups are rejected and counted
as failed. A `.psu` is read whole and every entry, name and size is checked.

`.psu` files are the optional **per-save versions** (Settings > Storage &
devices > MMCE backups > Per-save versions, default Off): after a card's history
is current, each save on its newest verified copy is kept as
`<card>/WLE-Saves/<save>/WLE-<stamp>.psu` when its bytes changed, named after
the card copy it came from (`src/mmce_save_versions.c`). They are uploaded like
the card copies, so Drive keeps one game's history apart from the whole card.
Because there can be many of them, each save folder keeps an `UPLOADED.TXT`
list of the versions already on Drive (Drive folder id, SHA-256, name). A
later sync hashes the local file and, when that exact entry is listed, skips
the check and the Drive lookup. Another account or a recreated Drive folder has
another id, so it uploads again. A version deleted by hand on Drive is not sent
again while its entry stays; delete `UPLOADED.TXT` to send it again.
Sync is copy-only: cloud files are never automatically deleted.

Each uploaded backup carries a private `wle_sha256` Drive `appProperties`
value. A backup is skipped only when the same folder already contains an item
with both the same filename and SHA-256; a same-name file with different
content is preserved as a separate Drive item. Legacy uploads without this
property may therefore be uploaded once more, after which subsequent syncs are
content-aware. The upload path hashes an already-open backup and streams that
same file handle to TLS, avoiding a filename re-open between content identity
and upload. Uploads use Drive's resumable-upload protocol; the session URL is
required to stay on `https://www.googleapis.com/` before the OAuth bearer token
is sent.

## Remote history browser and recovery

From **Tools > Google Drive backups**, choose **Browse cloud history...** to
inspect the app-created `WLE-MMCE-Backups` folder. The picker paginates Drive
metadata in groups of twelve and shows completed `WLE-*.mcd` / `WLE-*.ps2`
backup entries; incomplete `.PART` files are excluded. Select a snapshot with
a stored `wle_sha256` digest, choose an external USB/HDD folder (`usb`, `mass`,
`ata` or `hdd` paths) and confirm to download a new `RECOVER-CLOUD-###.mcd`
there. The app streams the complete file with authenticated TLS, enforces a
96 MiB size limit, checks the uploaded SHA-256 and PS2 card geometry, verifies
its local converted copy (PCSX2 `.ps2` images are converted), then publishes the
completed image. A failed download or verification cannot publish a final card.
The bearer token is sent only to `www.googleapis.com`; redirects to verified
Google content hosts carry no credentials.

A per-save version (`WLE-*.psu`) is recovered the same way, limited to 8.5 MiB,
and published as a new `RECOVER-CLOUD-###.psu` after its SHA-256 matches and its
entries are checked (`cloudGoogleRecoverSaveVersion`). It is not converted and
no card is written: **Card contents > Import .psu** puts it on a card, with a
safety copy first.

Snapshots without a stored digest can be inspected but not recovered through
this workflow. Google Drive history is never modified and downloaded backups
are **not** automatically activated or copied over an emulated MMCE card.
Direct in-place MMCE restore remains disabled until the active-card detection
and interrupted-write recovery guarantees can be established.

## Security

TLS certificate verification uses the PS2 system clock. Make sure the PS2 system date and time are reasonably correct; an invalid clock can make otherwise valid Google certificates appear expired or not yet valid. **Settings > Network > Sync PS2 clock from Internet** can set it.

TLS certificate verification is mandatory. PS2SDK's current `getrandom()`
path is not treated as sufficient by itself: Launchstation conditions its output
together with full-width COP0 Count samples, the PS2 RTC, action/runtime timing,
a local counter, and DNS/TCP timing through SHA-256 before Mbed TLS seeds its
CTR-DRBG. The PS2 has no documented general-purpose hardware TRNG, so this is a
defense-in-depth improvement rather than a claim of independently certified
hardware entropy. The refresh token is stored only on the user's configured
Launchstation settings device in `WLE-CLOUD.TOK` (with `WLE-CLOUD.TOK.NEW` used
only as a recoverable staging copy), unencrypted. Disconnect removes both local
token copies. Never publish either token file, and do not include them in bug
reports.

The built-in client ID and secret are compiled into the ELF. Google does not
treat installed/device client secrets as confidential; they identify the app
to Google and do not grant access to anyone's Drive without that user's
authorization.

## Update checks are separate

**Check for updates** shares the HTTPS client but not the Google trust files or
token. It contacts `api.github.com` and `github.com` using
`WLE-UPDATE-CA.PEM` (beside the running ELF, or in the dashboard settings
folder). See `docs-site/content/developers.html` (Native updater) for the
verification steps.

## Maintainer setup (once, for official builds)

CI embeds the client from the repository secrets `WLE_GOOGLE_CLIENT_ID` and
`WLE_GOOGLE_CLIENT_SECRET` via `scripts/gen_cloud_client.py`, which writes
`.cloud-deps/include/wle_cloud_client.h` (never committed). The two variables
must be set together, contain only `A-Za-z0-9._-`, and the ID must end in
`.apps.googleusercontent.com`. Builds without those secrets (forks, local
builds) have no built-in client and need a custom one. To create the project's
client:

1. In Google Cloud console, create a project (no billing account is
   required) and enable the **Google Drive API**.
2. Configure the OAuth consent screen (Google Auth Platform > Branding /
   Audience) as an **External** app and add only the `drive.file` scope.
3. Set the publishing status to **In production**. `drive.file` is a
   non-sensitive scope, so no Google verification review is needed. Apps left
   in **Testing** only work for listed test users and Google revokes their
   refresh tokens after 7 days.
4. Create an OAuth client of type **TVs and Limited Input devices** and add
   its client ID and secret as the two repository secrets.

Google does not treat installed/device client secrets as confidential;
keeping them in secrets rather than git only stops forks from reusing the
project's quota by accident. See [BUILDING.md](BUILDING.md) for the full
build.

## Source map

| File | Responsibility |
| --- | --- |
| `src/cloud_http.c`, `include/cloud_http.h` | Mbed TLS client, entropy hook, bounded requests, resumable upload, streaming and SHA-256 download with redirect limits (6) |
| `src/cloud_google.c`, `include/cloud_google.h` | OAuth device flow, token storage, Drive folder/file operations, sync, history listing, verified recovery |
| `include/cloud_google_roots.h` | Embedded Google Trust Services roots |
| `src/dash_tools.c` | Google Drive backups menu, link screen (with `src/qrcodegen.c`), update check |
| `src/main.c` | Automatic sync after IGR (`runWleIgrBackup`) |
| `windows/cloud-relay/` | Optional PC-side rclone relay for other providers |

## Tests

`tests/test_native_cloud.py`, `test_cloud_history_browser.py`,
`test_cloud_recovery.py`, `test_cloud_menu.py`, `test_cloud_link_screen.py` and
`test_cloud_relay.py` guard the contracts above; `test_self_update.py` guards
the updater. Real-hardware acceptance (DNS behavior, drive/adapter
combinations, long uploads) is separate from these host tests.
