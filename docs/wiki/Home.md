# LAUNCHSTATION wiki home

[Browsable GitHub Pages guide index](https://nathanneurotic.github.io/Launchstation/wiki.html) · [Current releases](https://github.com/NathanNeurotic/Launchstation/releases/tag/latest) · [Repository](https://github.com/NathanNeurotic/Launchstation)

This is the maintained Markdown guide collection stored in the repository. An automated sync publishes these guides and the authored website reference pages to the standalone GitHub Wiki when the Wiki repository is initialized and the publish workflow succeeds. GitHub Pages continues to build from `docs-site/content/` independently.

## Read by task

1. [Current interface and features](Current-Features.md) — four-tab dashboard, Home/Applications, Files, Tools, device discovery and core handoffs.
2. [MMCE recovery and safety](MMCE-Recovery.md) — verified local/Drive snapshot selection, new-file recovery and unsupported direct restore.
3. [Releases, updates and rollback](Releases-and-Recovery.md) — standard vs DS34, package sidecars, staging files and preserved baseline.
4. [Third-party credits, origin and licenses](../THIRD_PARTY_CREDITS.md) — all referenced upstream projects, bundled code and external integration roles.
5. [USB stability audit](../../USB-STABILITY-AUDIT-2026-10-08.md) — known risk areas and preserved hardware-tested baseline.
6. [Native Google Drive technical notes](../NATIVE_CLOUD.md) — OAuth/TLS, retention and recovery details.

## Source-of-truth policy

- Website text: `docs-site/content/*.html`, `docs-site/manifest.json`.
- Wiki-style explanations: `docs/wiki/*.md`.
- Package and feature behavior: actual `src/`, `include/`, `iop/` and verified Git history.
- Generated `gh-pages` output: **do not edit manually**.
- Hardware reports: identify exact build commit, console model, media transport and whether a behavior was hardware-tested. Passing CI alone does not prove compatibility.
