# Stable-baseline issue recovery

Base: `3e03e640a1434a6d23dd1b80498fa0fbb7c88672`, whose tree matches `2e140ab6baa720334a90da4aad031228ab3d9baa`. All work is scoped to `codex/stable-issue-recovery` and one review PR. Existing untracked work/build directories are retained. No hardware report is automatically closed and no release is published by this branch.

| Issue | Implementation or verified state | Remaining acceptance |
| --- | --- | --- |
| [#182](https://github.com/NathanNeurotic/Launchstation/issues/182) | The baseline already contains the maintained Rolling summary and generator. Verified the live `latest` release at `3e03e64` contains Downloads, Changelog, documentation/compare links and folded verification/hashes. Update the summary for this branch's final scope. | No release publication from this branch. |
| [#184](https://github.com/NathanNeurotic/Launchstation/issues/184) | Dedicated copy/delete overlay: action, full-width filename, remaining MB or completed selected items, themed progress and a full completion frame. Generic loading feedback retains its layout. | Host drawing/timing checks pass; console theme/background continuity remains pending. Blocking IOP calls cannot redraw before returning. |
| [#187](https://github.com/NathanNeurotic/Launchstation/issues/187) | Confirm ambiguous stat results through parent enumeration. Propagate directory open/read/close errors. Physical MC creation checks absence and strips the foreign EXCL bit. Preserve staging, close/readback/checksum, backup publication and rollback. Show reason, stage and numeric error with diagnostic context. | Host fault injection is not affected-console success. Reporter says 5 MB free; insufficient space is not an established diagnosis. |
| [#189](https://github.com/NathanNeurotic/Launchstation/issues/189) | Retain baseline wrapping and synchronize highlight/viewports in file browser, applications list, Files, Tools, Settings, picker, HDD Manager and modal menus. Adjacent movement remains animated. | Console long-list/controller coverage. |
| [#198](https://github.com/NathanNeurotic/Launchstation/issues/198) | Cull fully offscreen carousel cards with an outline/glow margin, avoiding their drawing, binds and artwork requests. Reference comparison verifies identical visible geometry and order over fractional positions. No background-time or quality-setting changes. | Removes demonstrated redundant work; not proof of the reporter's stall root cause. Repeat SELECT cycling on the affected console/theme. |
| [#200](https://github.com/NathanNeurotic/Launchstation/issues/200) | Keep Neutrino options audible until existing committed-launch cleanup. Preserve ordinary ELF muting, cancel and failure recovery. | Production-code host branches tested; physical sound output pending. |
| [#205](https://github.com/NathanNeurotic/Launchstation/issues/205) | Append dashboard IDs consistently across all nine catalogs. Localize Settings controls/pages, navigation, common actions, device labels and transfer progress at UI-owned call sites. Resolve the current language/override without retaining translated pointers. Explain PS2Link host files and bound long tab labels. | Some extended help/specialist dialogs remain English, explicitly stated by the Language page. Native-language and target-font/layout review pending. |

## Copy compatibility and safety

Installed SDK headers define `FIO_O_EXCL = 0x0800` and `sceMcFileAttrPDAExec = 0x0800`. MCMAN forwards flags to `McOpen` and preserves that bit as an attribute. Other filesystems retain their EXCL protection. No global driver-error or file-flag reinterpretation is introduced.

`copyLookupPath` uses stat success/ENOENT directly and preserves specific negative errors. Only ambiguous `-1` or positive nonstandard results use parent enumeration. Missing requires clean EOF and successful close; a found entry or case-only collision is preserved. Invalid roots, trailing slashes, malformed directory names and failed enumeration cannot authorize truncation.

Physical MCMAN has no atomic POSIX exclusive-create operation. Its final absence check/create cannot exclude another independent writer racing the same target name. Existing UI ownership/worker draining and staged replacement are retained; no cross-client filesystem locking is claimed.

Sources: [MCMAN mcdev.c](https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/mcdev.c), [ps2mc_fio.c](https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/ps2mc_fio.c), [libmc-common.h](https://github.com/ps2dev/ps2sdk/blob/master/common/include/libmc-common.h), and this repository's `iop/mmceman/src/mmce_fs.c`.

## Localization contract

IDs 0-467 retain their order and content. New IDs are appended to all nine tables with matching format signatures. Older external LNG files inherit built-in defaults for appended IDs. Missing/empty resolver values fall back to source text. Static UI metadata retains source labels rather than pointers into a replaceable external language buffer.

The renderer is unchanged. User filenames, application metadata, bookmarks, paths, config keys, protocol tokens, custom theme names and native language names remain outside automatic translation. Catalogs follow the existing ASCII convention. Untranslated extended help is disclosed and is not counted as completed coverage.

## Validation

CI includes the overlay, Neutrino-audio, carousel-culling and language fixtures alongside the existing host suite. Copy cases cover ambiguous missing-entry status, repeated replacement, enumeration failures, rollback and preservation of originals/recovery files. POSIX filesystem fixtures run under WSL/Ubuntu rather than Windows MinGW.

Host regressions, standard/DS34 builds, controller/UDPFS checks, loader layout and package verification are distinct evidence layers. No emulator or physical console result is claimed. Keep #187 and #198 open pending the affected-runtime report.
