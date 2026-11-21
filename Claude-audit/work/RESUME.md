# Resume notes for the Claude audit (paused 2026-11-16, low on credits)

This folder is the audit's working state. It lives in two places with the same layout:
- locally: the work root `<WORK_ROOT>` (all five repos, `src/<Repo>` clones, PR worktrees in `pr/<Repo>`);
- on each PR branch `claude-audit-findings`: `Claude-audit/work/` (that repo only). `scripts/publish.py` refreshes it on every local publish.

Scope: RiptOPL and Launchstation only. POPSLoader, Simple-Media-System and PS2-Servers are deferred; their draft PRs hold only a skeleton report.
The task itself (rules, categories, finding format, deliverables) is in `AUDIT-RULES.md` and `FINDINGS-FORMAT.md`; the agents' procedures are in `prompts/`.

## State at pause

| Repo | PR | Units | Read | Verified | Read, awaiting verify | Not read (with partial notes) |
|---|---|---:|---:|---:|---:|---:|
| RiptOPL | NathanNeurotic/RiptOPL#445 | 154 | 76 | 2 | 74 | 78 (12) |
| Launchstation | NathanNeurotic/Launchstation#397 | 103 | 53 | 2 (+1 with no findings) | 50 | 50 (7) |

Findings raised: RiptOPL 542 (21 verified), Launchstation 395 (13 verified, 1 refuted). The PR reports list verified findings first and the rest under "Not yet verified".
Not started: sweeps, merge, final render, `Claude-SUMMARY.md`, final PR title/body. Current numbers: `python -I scripts/status.py . <Repo>`.
Cost seen in the pilot: about 230k tokens per reader or verifier agent; units already read cost only a short check.

## Resume in a cloud session (only the GitHub repo available)

1. Work on the PR branch: `git fetch origin claude-audit-findings && git checkout claude-audit-findings`. `publish.py` refuses to commit on any other branch.
2. `sh Claude-audit/work/setup.sh` creates `Claude-audit/work/src/<Repo>` at the audited commit (a detached git worktree; `.gitignore` keeps it out of commits).
3. `cd Claude-audit/work`. Every command below runs from there. Use `python3` where it says `python` if needed. Read `AUDIT-RULES.md` before starting.
4. See what is left: `python -I scripts/status.py . <Repo> --missing`.
5. Read and verify the rest, either way:
   - With the Workflow tool (pass the absolute path of `Claude-audit/work` as `root`; `repo` keeps only this repo's units):
     `Workflow({scriptPath: "<root>/workflows/audit-read-verify.js", args: {"root": "<root>", "repo": "<Repo>", "part": "wave-A"}})`, then wave-B, wave-C, wave-D. Finished readers stop at their step 0 and their verifier runs.
   - Without it: take the units from step 4 one by one (core first). For each, a subagent (or you) gets the reader prompt below, then, if the unit has findings, the verifier prompt.
6. Save progress often: `python -I scripts/publish.py . <Repo> --with-unverified` renders the report, commits `Claude-audit/` (report and work state) with `[skip ci]` and pushes the branch.
7. When every unit is read and verified: sweeps and merge, `Workflow({scriptPath: "<root>/workflows/audit-sweep-merge.js", args: {"root": "<root>", "repos": ["<Repo>"]}})`. Without the Workflow tool: one agent per lens in `prompts/SWEEP.md` (dup, dead, size, ci, features), the verifier prompt for each `sweep-<lens>` unit, then one agent following `prompts/MERGE.md`.
8. Final report: `python -I scripts/publish.py . <Repo> --final`.
9. Summary (needs both repos): fetch the other repo's `findings/<Other>/_final.json` from its PR branch (`git clone --depth 1 -b claude-audit-findings https://github.com/NathanNeurotic/<Other> /tmp/other`, file `/tmp/other/Claude-audit/work/findings/<Other>/_final.json`) into `findings/<Other>/`, write the order of work to `tmp/order.md`, run `python -I scripts/summary.py . ../Claude-SUMMARY.md --order tmp/order.md`, commit it in both PRs.
10. Before a PR is merged, delete `Claude-audit/work/` (working state, about 10 MB); keep `Claude-audit/Claude-<Repo>.md` and the summary. Update PR titles and bodies with no AI attribution.

Reader prompt (replace `<root>`, `<Repo>`, `<unit>`, `<lens>`; lens is `defects` for ids ending in `.b`, `structure` for core and module tiers, otherwise the tier name):

> You are a reader in an exhaustive, read-only code audit of the <Repo> repository. Work root: <root>. Run every command from this folder. Your unit id: <unit>. Read these four files completely, in this order, before anything else: 1. <root>/AUDIT-RULES.md 2. <root>/prompts/READER.md 3. <root>/prompts/lens-<lens>.md 4. <root>/FINDINGS-FORMAT.md. Then carry out READER.md for repo <Repo>, unit <unit>, starting with its step 0. Final answer: ok only if the validator printed OK for the unit, with the number of findings.

Verifier prompt:

> You are a verifier in an exhaustive, read-only code audit of the <Repo> repository. Work root: <root>. Run every command from this folder. Unit to verify: <unit> (findings file: <root>/findings/<Repo>/<unit>.json; audited sources: <root>/src/<Repo>/). Read completely, in this order: 1. <root>/AUDIT-RULES.md 2. <root>/prompts/VERIFIER.md 3. <root>/FINDINGS-FORMAT.md. Then carry out VERIFIER.md for repo <Repo>, unit <unit>, starting with its step 0. <root>/brief/<Repo>.md says how the repo is built and what is live or legacy. Final answer: ok only if the validator with --verify printed OK, with the confirmed, corrected and refuted counts.

## Resume locally

Same steps from the local work root, for both repos at once: launch waves A-D without `repo` (or with `"root"` set to the local work root), `python -I scripts/publish.py . RiptOPL Launchstation --with-unverified` (it also refreshes `Claude-audit/work/` on both PRs), sweeps and merge with `audit-sweep-merge.js`, then `--final`, `summary.py`, and copy the deliverables to `<HOME>/Github/Claude-audit`. `scripts/publish_loop.py . 30 12` republishes every 30 minutes; delete `tmp/STOP_PUBLISH` before starting it.
