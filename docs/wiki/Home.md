# LAUNCHSTATION wiki home

[Browsable GitHub Pages guide index](https://nathanneurotic.github.io/Launchstation/wiki.html) · [Current releases](https://github.com/NathanNeurotic/Launchstation/releases/tag/latest) · [Repository](https://github.com/NathanNeurotic/Launchstation)

This is the maintained Markdown guide collection stored in the repository. An automated sync publishes these guides and the authored website reference pages to the standalone GitHub Wiki when the Wiki repository is initialized and the publish workflow succeeds. GitHub Pages continues to build from `docs-site/content/` independently.

## Read by task

1. [Current interface and features](Current-Features.md) — four-tab dashboard, Home/Applications, Files, Tools, Settings, device discovery, core handoffs, backups and languages.
2. [MMCE recovery and safety](MMCE-Recovery.md) — verified local/Drive snapshot selection, new-file recovery and unsupported direct restore.
3. [Releases, updates and rollback](Releases-and-Recovery.md) — the four rolling assets, how `latest` is published, standard vs DS34, the in-app updater and rollback.
4. [Themes and wallpaper example](Theme-and-Wallpaper-Guide.md) — 4:3 dimensions, PNG sample, safe-area tips and import instructions.
5. [Third-party credits, origin and licenses](../THIRD_PARTY_CREDITS.md) — all referenced upstream projects, bundled code and external integration roles.
6. [USB stability audit](../../USB-STABILITY-AUDIT-2026-10-08.md) — known risk areas and preserved hardware-tested baseline.
7. [Native Google Drive technical notes](../NATIVE_CLOUD.md) — OAuth/TLS, retention and recovery details.

## Quick facts

- Repository: <https://github.com/NathanNeurotic/Launchstation> · Site: <https://nathanneurotic.github.io/Launchstation/>
- Fork of [wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z); the dashboard is the only interface.
- Releases: one moving `latest` release with `APP_LAUNCHSTATION.psu/.zip` and `APP_LAUNCHSTATION-DS.psu/.zip`, built from `master` by CI with the PS2BUILD LLVM/Clang toolchain.
- History: [CHANGELOG.md](../../CHANGELOG.md) (the Rolling section feeds the release notes).

## Source-of-truth policy

- Website text: `docs-site/content/*.html`, `docs-site/manifest.json`.
- Wiki-style explanations: `docs/wiki/*.md`.
- Package and feature behavior: actual `src/`, `include/`, `iop/`, `ps2.yaml`, `.github/workflows/` and verified Git history.
- Generated `gh-pages` output: **do not edit manually**.
- Hardware reports: identify exact build commit, console model, media transport and whether a behavior was hardware-tested. Passing CI alone does not prove compatibility.
