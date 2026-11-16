# Selected saildot4k wins — 15 November 2026

This implements the selected behavior from the 48-hour branch audit ending
2026-11-15 10:23:09 UTC. The baseline is Launchstation master
`70580eb37e732e74402bb6b43d814c108c15d667` (PR #387). The source branches were
frozen at `master` e4d0ece22a, `less-ee-ram-save-file-extraction-and-creation`
c9c881c435 and `code-cleanup` 9b764cff2b. The generated `gh-pages` branch was
also inventoried. All five wLaunchELF_R3Z branches were checked; none had an
in-window commit. Older R3Z branch work is outside this collection.

The changes below adapt saildot4k's work to Launchstation's existing APIs,
storage protections, sound themes, save formats, networking and localization.
They do not merge the fork branches wholesale.

## Coverage and provenance

| Source work | Result in Launchstation |
|---|---|
| [c4f08b1a](https://github.com/saildot4k/Launchstation/commit/c4f08b1aae4cc23e85c2896c20dc482f7a53c79e), [c2ccf210](https://github.com/saildot4k/Launchstation/commit/c2ccf2103d33f1611a44fb7eecc6ff779e57e01e), [4c3aa45b](https://github.com/saildot4k/Launchstation/commit/4c3aa45b38807dfa277489cf190691889f640e93), [006645b9](https://github.com/saildot4k/Launchstation/commit/006645b9ca61f5633ab2fc9ec8ea046a65b4a0ea) — icons, IPv4 wrap, held shoulder input and synchronized app scrolling | Already adapted by #387. Keep its tests, languages and larger icon atlas. |
| [61102f37](https://github.com/saildot4k/Launchstation/commit/61102f37bcb4ce74848777f3d0fcc4a4ac301af1) — driver-stage feedback | Existing `dashMsgHook` routes active driver status to the loading overlay, whose unknown-length state has a spinner and fitted text. Keep this interface rather than adding the fork's browser-specific status field. |
| [871195ea](https://github.com/saildot4k/Launchstation/commit/871195eacab2427cdbc5f323b50608f5e807678a) — Neutrino lifetime, discovery, MMCE and audio | Snapshot the RetroGEM preference before cleanup frees settings. Prepare each candidate location before probing later locations. Return explicit MMCE switch/readiness failure to the ISO caller. Queue audio initialization/theme uploads/BGM reads through the existing shared worker. |
| [134e83f8](https://github.com/saildot4k/Launchstation/commit/134e83f83544696f34aa68b0b57b4cb145371b90), [4dc1a654](https://github.com/saildot4k/Launchstation/commit/4dc1a65445d781b149a39cb2b62df858061262fb) — exec UI lifetime and feedback | #387 already stops UI callbacks before freeing settings/fonts. The lifetime test now frees settings at the actual cleanup boundary. Keep visible MMCE waits and returned errors; the fork's blanket quiet-launch wrapper is not adopted. |
| [8e5e97a8](https://github.com/saildot4k/Launchstation/commit/8e5e97a8c220dcfd8e632a3e40c06ba5f4e65074) — quiet audio work; older [9adc685a](https://github.com/saildot4k/Launchstation/commit/9adc685a86c773727aab8b3ea77bb0c01dbe94df) — first-visible-frame startup | Routine audio preparation has no visible job popup. Retain the existing first-present/fade/autostart policy and deferred boot sound. Drain accepted audio work before shutdown and reject stale IOP generations. |
| [f5e6974e](https://github.com/saildot4k/Launchstation/commit/f5e6974e857ecbedfa617d826687d317617be975) — VMC/menu/icons | Share the PS2 suffix eligibility list between browsers; hide unusable classic Mount and argument actions with correct menu height/cursor position. Compatibility/debug/logo settings use the Settings icon. Save already used the Save icon. |
| [82822eaa](https://github.com/saildot4k/Launchstation/commit/82822eaab041a8c34a4086a70e2b53a59a762a3b) — save streaming and source verification | Add metadata-only card reads and bounded PSU/PS2 PSV output, incremental signing, replay verification and one open file at a time. Single/batch image-to-image moves hash the copied snapshot and reread/hash the source before deletion; changed or unreadable sources remain. Preserve safety copies and active-MMCE-image protection. |
| [c9c881c4](https://github.com/saildot4k/Launchstation/commit/c9c881c435b5286dd93ac08cdf7aa3733bb4a521) — whole-save progress and arguments | Physical PS2-card import uses one stable save label, batch ordinal and write-plus-readback total. A final sentinel completes only after all closes and metadata restoration. Online CF text can supply arguments. Classic editor-created documents can supply their current buffer without first saving. |
| [c627ecc5](https://github.com/saildot4k/Launchstation/commit/c627ecc5213072671aab89035289a285e0b1119a) — traversal and directory errors | Get Size uses one shared listing, compact retained children, a 16-directory limit, a 2 MiB heap budget and overflow checks. Preserve partial-size/error reporting and network stat fallback. Directory readers reject unterminated names and propagate close failures while retaining device-specific end markers and browser partial listings. |
| [04d4dcb3](https://github.com/saildot4k/Launchstation/commit/04d4dcb3b8f9cbb4cd94f145dbc61bfa336aa47a), [9b764cff](https://github.com/saildot4k/Launchstation/commit/9b764cff2b8be8c13a70e90cb0371958bba15822) — split-module include fixes | The corresponding modules are not split here. New code includes its owning declarations directly; the full card-tool host harness compiles with implicit-declaration errors enabled. |
| [513504b3](https://github.com/saildot4k/Launchstation/commit/513504b3b0d2dbd11947016995feb06a1ff53dde) — initial double buffering | Already implemented by 151b960 / #361; retain the current screen-generation and texture handling. |

## Deliberate adaptations and limits

- Neutrino still requires a readable ELF plus modern or legacy `system.toml`.
  SMB games still require a local core and their network configuration/modules.
  Lazy search keeps configured path, game device, mc0, mc1 priority.
- MMCE retains manager-only bring-up, source-slot/alias and MX4SIO guards,
  and fifteen cooperative one-second readiness polls per responding adapter.
  Absent/unsupported adapters remain optional; a responding adapter whose switch
  fails or never reports ready prevents launch. No new firmware support is claimed.
- VMCMAN mounts PS2 images. `.BIN`, `.VMC`, `.MC2`, `.PS2`, `.MCD`, `.VM2` and
  `.RAW` are candidates; contents still determine whether mounting succeeds.
  PS1 containers remain in Card contents, not the PS2 mount menu.
- Streaming eliminates full payload/archive duplication for PSU/PS2 PSV creation
  from card images. Its payload buffer is 128 KiB; metadata allocations scale with
  entry count. Conversion may still hold decoded input. Compressed import, CBS/ZIP
  output and the bounded PS1 save/card representations keep their existing APIs.
  This is not a claim that every save operation now streams.
- Existing card imports refuse same-name destinations and preflight required space.
  No direct delete-and-replace workflow or speculative reclaimable-space credit is
  introduced. PSU/PS2 PSV replacements stream and verify a new `.PART` before
  renaming the previous export to `.BAK` and publishing the new file. Failed or
  unsupported publication retains recovery files; existing `.PART`/`.BAK` files
  are never overwritten. This is not a power-loss atomic replacement transaction.
  Moves recheck the source after its safety copy, through the same handle used
  for deletion. Network sources and sources shared by running FTP/SMB servers
  are retained because these interfaces cannot exclude concurrent writers.
- Empty argument text still reports unusable input and does not launch with an
  empty list. Argument parsing retains its existing pending-list reset behavior. Keep Up=start and Down=end. Preserve About/build identity.
- Retain existing deletion, exploit, host-write, SMB and storage-stack safeguards.
  Pure module moves, a new deletion walker, generated site output, merge bookkeeping
  and the canceled lazy-search experiment/revert are not imported.

## Validation

The host tests execute production functions and the full audio/card modules:

- Neutrino cleanup invalidates the settings pointer before handoff; MMCE tests cover
  absent adapters, failed commands/status reads, busy timeouts and protected sources.
- Audio tests cover queued initialization, exclusive RPC ownership, theme/volume
  changes while queued, optional sidecar/short-I/O failures, generation recovery,
  rejected submission, separate sound/music buses, boot requests deferred before
  job submission, queue-drain boundaries and signer-reset shutdown draining.
- Independent PSU/PSV layout and HMAC oracles remain separate from the streaming
  encoder. Streaming output also matches the existing buffered encoders.
- A 5 MiB save exports with a 128 KiB allocation ceiling and one descriptor, including
  short I/O, read/write/close/allocation failures, corrupted readback and source aliases.
- Full card-tool integration runs under AddressSanitizer/UndefinedBehaviorSanitizer,
  including sources mutated during destination writes and after the source safety
  copy; their saves and destination copies remain. Active-server and network-source
  tests cover refusing deletion when exclusive access cannot be established.
- Import progress tests cover multiple/empty files, write/read close failures,
  metadata failure, cleanup, space refusal and an existing destination.
- Browser and traversal tests cover eligible suffixes, CF online/offline, hidden rows,
  complete/partial listings, driver end markers, depth/heap limits and size overflow.

Local target validation uses LLVM with ps2build v2026.11.06. The repository's CI
retains its independently pinned SDK. Standard/DS34 builds and linked variant,
toolchain purity, stack realignment and embedded-loader layout checks are required.
Host and build evidence does not establish physical PS2, firmware, TV/audio,
emulator or gameplay acceptance; none was run for this collection.

## Additional branch audit

The follow-up inventory includes `vu0-and-1-usage` at
`fcb6e2667cb8bb06b1f66f3e27d9afcc1bc5f569`; the other branch heads remain
those recorded above. Two small browsing fixes from that branch are adapted:

- [f4605ad9](https://github.com/saildot4k/Launchstation/commit/f4605ad9b2906acbf452861d3706a5cc5faeb230):
  retry transient PS1-card detection errors within the existing four-attempt,
  100 ms retry window. Failed RPC synchronization cannot reuse a previous
  result. Directory request-start and synchronization failures remain errors;
  permanent permission/format failures are not retried. The production-reader
  ASan/UBSan test covers both slots, retries, exhaustion and failed requests.
- The directory-reader portion of
  [7d019bd7](https://github.com/saildot4k/Launchstation/commit/7d019bd7e75c4c05801381dd685f2bf7f57a2c97):
  reject invalid local MMCE directory handles, clear each returned entry before
  decoding its bounded name, and treat unknown directory statuses as errors.
  Existing EOF and iterator-zero handling was already present. Sanitizer
  coverage also exposed signed left shifts when decoding high-byte metadata;
  unsigned casts preserve the wire bits without undefined behavior.

The PS1 recovery test fails against the previous implementation. The MMCE
wire test now checks cleared names, invalid handles, unknown status and high
metadata bytes under ASan/UBSan. Firmware timing and physical PS1-card opening
remain hardware acceptance items.

Review follow-up: `readGENERIC` now propagates explicit MMCE directory errors
such as `-EIO` and `-EBADF` to complete-list callers. The browser retains the
entries it already read, while copy/delete/Get Size cannot treat a partial read
as complete. MMCE EOF is zero; the existing `-1` timeout/reply exemption and
CD-specific end marker remain. The production-reader regression covers both
MMCE slots, errors before and after entries, recovery and empty directories.

The VU0/VU1 renderer, benchmark/profiling infrastructure and new background
quality preference are separate graphics work. They are not imported into
this save/browsing merge-readiness change without measured target performance
and hardware acceptance. Generated `gh-pages` output and pure code moves also
remain outside this selection.
