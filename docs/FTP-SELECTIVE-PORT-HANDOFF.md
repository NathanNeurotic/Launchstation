# Selective FTP port: unfinished pickup checkpoint

User approved all four wins from saildot4k/Launchstation ftp-fixes, preserving our own features. An FTP status screen is acceptable; restrictions require concrete reasons. This is a draft work-in-progress, NOT merge-ready or a tested fix.

## Pinned sources and ownership

- Base: NathanNeurotic/Launchstation master21fa80a9f32b5f2ba6a29f681c45c79914c61884.
- Fork reviewed:7b2c2cde8d9ed858a65c283f16028457d64059c1.
- Earlier fork FTP commits already selectively integrated by our0165ec7/PR267. Do not redo those.
- Branch codex/selective-ftp-wins; worktree C:/Users/natha/Github/Launchstation.worktrees/selective-ftp-wins.
- Review report C:/Users/natha/Github/reports/launchstation-ftp-fixes-audit-20261104.md. Two reference worktrees audit-ftp-current-20261104 and audit-ftp-fork-20261104.
- Preserve all other checkouts. No code from this draft has been merged or released.

## Approved goals

1. Consistent virtual /hdd/<unit>/<partition>/ paths for every operation, preserving virtual CWD/parent navigation.
2. PFS mount pinning/ownership, including multiple FTP clients and the concurrent EE browser.
3. Demand-driven device preparation before accepting FTP clients; retain all supported devices/aliases and existing driver conflict rules.
4. FTP status/Stop lifecycle, retaining UI access, PS2Link, remote command and staged/resumable ISO workflows. Screen may be modal but must not silently remove those workflows.

## Current implementation sketch (needs review/correction)

- Adapted fork partition translation/reference-counted mounts into our FileSystem.c, retaining our discovery whitelist, /mass aliases, SITE methods, pending-transfer guard, ABOR426/226 behavior and reply bounds.
- Proposed disjoint physical PFS0..3 EE and PFS4..7 FTP, with FTP logical /pfs/0..3 mapped onto4..7. Standard PFS loader maxmount changed4to8, DVR separate driver stays4. This is a PROPOSAL needing SDK capacity/memory and all mount-path verification, not an established safe interface. Reconsider if a smaller ownership solution exists. Must not allow legacy raw paths or SITE to remount EE-owned slots. SITE-owned identities currently use a truncated40byte source buffer: review validation and identity reuse carefully.
- Added fork FtpControl IOP status/cooperative-stop service, 4KiB control thread, owner cleanup/STOPPED path, imports and192byte protocol. Preserved our transfer command handling.
- Added EE ftp_session.c with asynchronous RPC and bounded waits, static DMA buffers retained aftertimeout, generation checks. Needs real-source host fixtures and native compile. Check late bind completion/retry, timeout packet lifecycle, cache handling, reentrancy, failure reporting and stopped-state transitions.
- init.c prepares compatible ATA/HDD/USB/iLINK before networking, keeps netfs; no forced cold IOP reset at entry. ResetRuntimeDeviceState requests FTPshutdown before reset; failure refuses destructive transition. Need trace ALL direct Reset/handoff/poweroff paths and network/driver switches. Not all paths are covered.
- New status panel has Stop and Back/Keep running. Stop closes FTP then resets IOP to unload resident module. Back returns with service running. Added dashFtpPollRemote wrapper to keep workflows, but see unfinished gaps below.

## Known failing validation / unfinished work

- Current adapted tests/test_ftp_server.py compiles production IOP startup and host units, then FAILS at test_hdd_listing assertion FileSystem_ChangeDir('/hdd/0/__common/'). The old fixture modeled only sharedPFS0/CWD behavior and must now model actual partition getstat and private mounts. Investigate production logic too; do not weaken assertions to hide a defect. Check HDD driver leading slash semantics and unit propagation. fork test fixture has suitable mount-aware patterns.
- Existing test fixture PFS nodes are logical units; a provisional mapping subtracts FS_PFS_BASE in find/add. Directory iteration still compares physical f->unit directly against logical nodes. Correct mock consistency and assert physical ownership explicitly.
- Fork-only source tests passed during audit, but NOT proof this adaptation passes. Need import/adapt meaningful fork virtual-HDD/mount/Stop tests while retaining our stronger pending-transfer/ABOR/device tests.
- New raw /pfs logical mapping needs root-list/unit-probe path review (old enumeration calls deviceops directly). Balance references on every failure/one-shot path; review Close, OpenDir failure, GetFileSize, rename across slots, SITE errors/cleanup, Sync.
- Pending rename records physical slot+generation. Verify rename source stays pinned while resolving destination, including all-slots-full and destination selection/eviction cases.
- Device startup: check incompatible MMCE/MX4SIO/block-stack transitions and getIpConfig reads, hardware-gated HDD/ATA, missing devices, IOP generation changes. Do NOT eagerly replace active MMCE/MX4SIO stack merely to prepare another device. Existing storage functions may reset; trace sequence.
- dashFtpPollRemote calls existing pollPcGameReceiver/pollRemotePcCommand; these currently require sectionHOME. Tool screen can open from another section, so wrapper presently does NOT guarantee polling. Correct without bypassing safe worker/IOP gates. Receiver can reset IOP and restore FTP; screen must detect generation and cleanly return. Remote app handoff must safely retire FTP first.
- Panel currently polls at100ms and uses loading flag while invoking remote handlers that may use their own overlays. Review nested loading/modal state, audio/texture IO and legacy/non-dashboard invocation. No startup-error modal if startup simply leaves isFtpNetworkReady false yet; fix.
- Safe Stop timeout does not guarantee recovery from hung drivers. Do not force reset over unacknowledged files. Need explicit bounded behavior and accurate user message.
- No new EE transport/screen host fixture yet, no added CI wiring for those new tests, no updated user docs, no formatting pass. Only git diff--check passed at checkpoint.
- Native LLVM ps2build was started via MCP but not completed at time of writing. Do not claim it passed. Check output in local build and rerun appropriately. Fresh worktree may lack generated font/hash/assets/cloudTLS/JPEG dependencies; distinguish environment setup from code diagnostics.
- No real PS2 or emulator testing performed. No measured binary/static/IOPheap delta. Added mount slots and control thread require resource accounting.

## Next actions

Read applicable instructions and source first. Fix the failing ownership fixture and implementation with before/after controls, finish lifecycle/remote integration and native builds, measure resource changes, update docs and tests/CI. Keep changes minimal; this checkpoint was pushed early at the user's explicit request for another agent. Request CodeRabbit after a coherent validated head, handle justified findings, then exact-head check all CI/reviews/mergeability before proposing ready status. Do not merge this WIP.
