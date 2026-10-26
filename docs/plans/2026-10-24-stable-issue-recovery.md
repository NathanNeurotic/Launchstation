# Stable-baseline issue recovery

> **Status as of 2026-10-26: implemented and merged (PR #206, `b815d10`); this is the current file-copy contract.**
> Direct overwrite, 9-language catalogs (IDs appended after 467), carousel culling and the copy/delete overlay
> are in tree. #187 and #198 are intentionally kept open pending reporter confirmation. #182 release summary
> is maintained in `CHANGELOG.md` and `scripts/release_notes.py`.


Base: `3e03e640a1434a6d23dd1b80498fa0fbb7c88672`, whose tree matches `2e140ab6baa720334a90da4aad031228ab3d9baa`. All work is scoped to `codex/stable-issue-recovery` and one review PR. Existing untracked work/build directories are retained. No hardware report is automatically closed and no release is published by this branch.

| Issue | Implementation or verified state | Remaining acceptance |
| --- | --- | --- |
| [#182](https://github.com/NathanNeurotic/Launchstation/issues/182) | The baseline already contains the maintained Rolling summary and generator. Verified the live `latest` release at `3e03e64` contains Downloads, Changelog, documentation/compare links and folded verification/hashes. Update the summary for this branch's final scope. | No release publication from this branch. |
| [#184](https://github.com/NathanNeurotic/Launchstation/issues/184) | Dedicated copy/delete overlay: action, full-width filename, remaining MB or completed selected items, themed progress and a full completion frame. Generic loading feedback retains its layout. | Host drawing/timing checks pass; console theme/background continuity remains pending. Blocking IOP calls cannot redraw before returning. |
| [#187](https://github.com/NathanNeurotic/Launchstation/issues/187) | Restore upstream R3Z direct overwrite: remove destination, create with WRONLY/TRUNC/CREAT, copy and close. Remove the custom stat/staging/checksum/backup/rename transaction and its workarounds. Retain dashboard progress and readable errors. | Host cross-device tests and target builds do not establish affected-console acceptance. |
| [#189](https://github.com/NathanNeurotic/Launchstation/issues/189) | Retain baseline wrapping and synchronize highlight/viewports in file browser, applications list, Files, Tools, Settings, picker, HDD Manager and modal menus. Adjacent movement remains animated. | Console long-list/controller coverage. |
| [#198](https://github.com/NathanNeurotic/Launchstation/issues/198) | Cull fully offscreen carousel cards with an outline/glow margin, avoiding their drawing, binds and artwork requests. Reference comparison verifies identical visible geometry and order over fractional positions. No background-time or quality-setting changes. | Removes demonstrated redundant work; not proof of the reporter's stall root cause. Repeat SELECT cycling on the affected console/theme. |
| [#200](https://github.com/NathanNeurotic/Launchstation/issues/200) | Keep Neutrino options audible until existing committed-launch cleanup. Preserve ordinary ELF muting, cancel and failure recovery. | Production-code host branches tested; physical sound output pending. |
| [#205](https://github.com/NathanNeurotic/Launchstation/issues/205) | Append dashboard IDs consistently across all nine catalogs. Localize Settings controls/pages, navigation, common actions, device labels and transfer progress at UI-owned call sites. Resolve the current language/override without retaining translated pointers. Explain PS2Link host files and bound long tab labels. | Some extended help/specialist dialogs remain English, explicitly stated by the Language page. Native-language and target-font/layout review pending. |

## Upstream file-handling restoration

On 2026-10-25 the maintainer explicitly requested upstream wLaunchELF_R3Z file handling and removal of the custom replacement transaction. This supersedes the earlier staged-replacement design and validation notes.

Reference: saildot4k/wLaunchELF_R3Z master `06b06d000072d358c93d98104d64d2f081e47ba7`, `src/filer_copy.c` and `src/filer_fileops.c`.

Normal and recursive file replacement now use the upstream operation sequence: `genRemove(out)`, then `genOpen(out, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT)`, transfer and close. No destination-stat preflight, parent-directory scan, exclusive-create flag, sibling staging file, checksum verification, backup publication or rename is required. Remove the FatFs stat reinterpretation introduced for that transaction; `genGetStat` again passes through driver results. Existing device-path integration remains.

The dashboard progress/error presentation, worker/device preparation, source handling and supported-device metadata dispatch remain integrated. The follow-up restores upstream's single read/write per chunk, buffer sizing, and retention of the output after close failure. Custom short-I/O completion loops and the dashboard-specific MC/VMC buffer cap are removed. Same-path protection avoids deleting the source. MMCE's unsupported timestamp operation is skipped. No user firmware change is required by copying or replacement.

Scope: normal copy/move/delete/rename, folders, metadata and their dashboard call paths. The inherited rename/delete operations remain in use; cut/move deletes its source only after successful copying. Preserve CF/i.Link support, driver lifecycle coordination, launcher handoffs, timestamp-editor UI, backup/history/recovery features, translations and dashboard rendering. This correction does not revert those features or claim their complete source identity with upstream.

Direct replacement has upstream's failure behavior: once the old destination is removed, a failed copy does not retain the original. There is no automatic backup or rollback promise. Existing unrelated backup/history features are unchanged.

## Localization contract

IDs 0-467 retain their order and content. New IDs are appended to all nine tables with matching format signatures. Older external LNG files inherit built-in defaults for appended IDs. Missing/empty resolver values fall back to source text. Static UI metadata retains source labels rather than pointers into a replaceable external language buffer.

The renderer is unchanged. User filenames, application metadata, bookmarks, paths, config keys, protocol tokens, custom theme names and native language names remain outside automatic translation. Catalogs follow the existing ASCII convention. Untranslated extended help is disclosed and is not counted as completed coverage.

## Validation

CI includes the overlay, Neutrino-audio, carousel-culling and language fixtures alongside the existing host suite. Copy cases cover repeated direct overwrite, smaller and empty replacements, cross-device MC/MMCE/USB copies, source failures, write/close failures and recursive directories. Destination stat and rename adapters abort if unexpectedly called. POSIX filesystem fixtures run under WSL/Ubuntu rather than Windows MinGW.

Host regressions, standard/DS34 builds, controller/UDPFS checks, loader layout and package verification are distinct evidence layers. No emulator or physical console result is claimed. Keep #187 and #198 open pending the affected-runtime report.
