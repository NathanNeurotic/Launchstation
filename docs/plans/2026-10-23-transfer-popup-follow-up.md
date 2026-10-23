# Transfer popup and list verification follow-up

Base: master f0ea7d1. Keep this follow-up in one draft PR, under 100 files, without requesting CodeRabbit or closing hardware reports.

## Confirmed changes

- #184: file operations use a compact three-row layout (action heading, full-width filename, then remaining MB or completed selected-item count) and the shared themed progress bar. Generic device-loading overlays retain their spinner/layout. The initial filename is drawn before blocking copy I/O. A completed operation draws the full bar before closing; intermediate values continue to ease. New same-named files reset their progress after completion.
- Deletion remains selected-item based. Recursive deletion updates the current filename and retains the outer selected-item count. No recursive pre-count scan, asynchronous deletion engine or invented byte percentage is introduced. A single blocking filesystem RPC can still pause rendering until it returns.
- #189: shared long modal menus snap the highlight into a newly selected viewport after wrapping/jumping. Ordinary adjacent movement still eases; disabled entries are skipped. Other major list controllers already implement wrap-around and are not rewritten.
- #187: common failures receive distinct plain-language reasons (space, access denied, read-only, missing path/device, I/O, timeout, memory and existing recovery target) while stage, numeric code, device pair, byte position and build information remain available. Unknown failures keep their original code; they are not diagnosed as insufficient space. Transfer/replacement logic is unchanged.

## Verification

The new modal regression fails on master f0ea7d1 because its first wrapped highlight is clipped. It passes with this fix in both directions and with L2/R2 jumps and disabled endpoints. The production loading renderer is executed with a host draw sink: bounds, separate action and full-width filename rows, immediate filename, 64-bit MB formatting, intermediate easing, full final fill, same-name reset, generic spinner and scene tick ownership are checked. A generated HTML draw-call preview is explicitly a host approximation, not a console screenshot.

The full clipboard/copy fault-injection fixture checks plain-language diagnostics and preserves numeric errors, original files and recovery behavior. Local verification passed all 116 host scripts, standard and DualShock GCC builds with TechWritesCode PS2BUILD SDK v2026.10.15, linked controller-variant checks and UDPFS peer ABI checks. The site generator validated 35 pages. The PR records CI separately for the pushed head. The error popup keeps stage/code and reason in its main body and places the remaining diagnostic context on its existing scrolling detail line.

## Console acceptance

Use disposable files and preserve existing settings, ELFs and saves. Test fresh/repeated/low-space MC0 and MC1 copies, replacement cancellation and recovery; USB/HDD/network sources where available; small and large files; multiple selected files and a single nested folder deletion; NTSC/PAL and light/dark/custom themes. Confirm the actual popup has no legacy throughput/ETA clutter, the filename is visible before transfer, remaining MB decreases, the bar completes and the background advances between returning I/O calls. Verify shared modal wrapping in both directions with long lists and disabled entries.

Keep #184/#187/#189 open until the applicable behavior is confirmed. The original reporter's MC0 failure is still not conclusively attributed to space; the new error messages support a precise retest.
