#!/usr/bin/env python3
"""Render concise rolling-release notes from first-parent changes and packages."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess

ASSETS = (
    "APP_LAUNCHSTATION.zip", "APP_LAUNCHSTATION.psu",
    "APP_LAUNCHSTATION-DS.zip", "APP_LAUNCHSTATION-DS.psu",
    "LAUNCHSTATION.ELF", "LAUNCHSTATION-DS.ELF", "BOOT.ELF", "BOOT2.ELF",
    "LAUNCHSTATION.zip",
)
# The full kit depends on an upstream download (the latest Neutrino release) and
# the pinned externals. When it could not be built, the build still publishes
# every other asset and the notes say the previous kit stays attached.
OPTIONAL_ASSETS = ("LAUNCHSTATION.zip",)
# The in-app "Check for updates" downloads this release's JSON through a fixed-size
# buffer (256 KiB). The body is the only part that grows, so the changelog list is
# cut to a byte budget (newest entries first; the full file stays in the repo) and the
# finished body must stay under a hard limit or the build fails before publishing.
CHANGELOG_BUDGET = 12000
COMMIT_BUDGET = 4000
BODY_LIMIT = 40000



def published_name(name, version):
    """Every published ZIP name ends with the build number; PSUs and ELFs keep theirs."""
    if version and name.endswith(".zip"):
        return f"{name[:-len('.zip')]}-{version}.zip"
    return name


def markdown(text):
    return re.sub(r"([\\`*_[\]<>])", r"\\\1", text.strip())


def changes_from_log(log, repo):
    """One entry per master change; omit branch-internal fixup history."""
    changes = []
    for record in log.split("\x1e"):
        record = record.strip()
        if not record:
            continue
        sha, body = record.split("\x00", 1)
        lines = [line.strip() for line in body.splitlines() if line.strip()]
        title = lines[0] if lines else "Source update"
        pr = re.match(r"Merge pull request #(\d+)(?::\s*(.*)| from .*)$", title)
        if pr:
            number = pr.group(1)
            title = pr.group(2) or (lines[1] if len(lines) > 1 else f"Update #{number}")
            url = f"https://github.com/{repo}/pull/{number}"
        else:
            url = f"https://github.com/{repo}/commit/{sha}"
        title = re.sub(r"^(?:feat|fix|docs|ci|test|chore|refactor)(?:\([^)]*\))?!?:\s*", "", title)
        title = title[:1].upper() + title[1:]
        changes.append(f"- [{markdown(title)}]({url})")
    return changes


def rolling_changelog(text):
    """Use the maintained public summary, stopping before older releases."""
    match = re.search(r"^## Rolling\s*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not match:
        return []
    return [line.strip() for line in match.group(1).splitlines()
            if line.lstrip().startswith("- ")]


def revision_changelog(revision):
    summary = subprocess.run(["git", "show", f"{revision}:CHANGELOG.md"],
                             capture_output=True, text=True)
    return rolling_changelog(summary.stdout) if summary.returncode == 0 else []


def bound_changes(changes, repo, revision, budget=CHANGELOG_BUDGET):
    """Keep the newest entries that fit the byte budget; link the full changelog for the rest."""
    kept, used = [], 0
    for index, line in enumerate(changes):
        used += len(line.encode("utf-8")) + 1
        if used > budget:
            link = f"https://github.com/{repo}/blob/{revision}/CHANGELOG.md"
            while kept and not kept[-1]:
                kept.pop()
            return kept + ["", f"- Older entries are in the [full changelog]({link}) ({len(changes) - index} more lines)."]
        kept.append(line)
    return kept


def release_changes(repo, revision, previous, log):
    """Rolling summary (budgeted) plus, when it did not change, links to the new commits (budgeted separately)."""
    current = revision_changelog(revision)
    if not current:
        return bound_changes(changes_from_log(log, repo), repo, revision)
    summary = bound_changes(current, repo, revision)
    # Keep the public rolling summary visible on every release page.
    # If the summary text did not change but new commits exist, append those
    # commit links so fresh changes are still visible even when the summary fills its budget.
    if not previous or revision == previous or current != revision_changelog(previous):
        return summary
    commit_changes = changes_from_log(log, repo)
    if commit_changes:
        return (summary + ["", "- Additional merged changes in this rolling build:"]
                + bound_changes(commit_changes, repo, revision, COMMIT_BUDGET))
    return summary


def render(repo, revision, previous, run_id, changes, digests, version="", omit_kit=False):
    base = f"https://github.com/{repo}"
    names = [published_name(name, version) for name in ASSETS]
    compare = f"{base}/compare/{previous}...{revision}" if previous else f"{base}/commits/{revision}"
    workflow = f"{base}/actions/runs/{run_id}"
    download = f"{base}/releases/download/latest"
    changelog = "\n".join(changes) or "- Rebuild of the current revision; no new changes."
    hashes = "\n".join(f"{digests[name]}  {published}" for name, published in zip(ASSETS, names)
                       if name in digests)
    heading = (f"**Launchstation build `{version}`** (`{revision[:7]}`). Every ZIP name ends with this build number.\n\n"
               if version else "")
    if "LAUNCHSTATION.zip" in digests:
        kit_row = f"| Full kit: standard app + POPS, Ember, Neutrino | [{names[8]}]({download}/{names[8]}) | included | included |\n"
        kit_note = ("The full kit is laid out like RiptOPL's release ZIP (`APPS/APP_LAUNCHSTATION/`, `APP_LAUNCHSTATION.psu`, "
                    "`POPS/`, `EMBER/`, `neutrino/`): copy it to the root of a game device. Its `KIT-README.txt` explains "
                    "each folder; no BIOS or game is included.")
    else:
        kit_row = ""
        if omit_kit:
            kit_note = ("The optional full kit for this build was omitted on a publication retry because the first "
                        "archived build had no matching kit. The previous rolling-release kit remains attached; "
                        "the standard/DS34 app packages are unaffected.")
        else:
            kit_note = ("The full kit `LAUNCHSTATION-<build>.zip` was not rebuilt for this build (the Neutrino download or the "
                        "kit check did not succeed); the previous build's kit stays attached below and holds the same POPS, "
                        "Ember and Neutrino folders with the previous app build.")
    return f"""{heading}## Downloads

| Version | ZIP | PSU | ELF |
| --- | --- | --- | --- |
| Standard | [{names[0]}]({download}/{names[0]}) | [Download]({download}/{names[1]}) | [Download]({download}/{names[4]}) |
| DS34 USB/Bluetooth controllers | [{names[2]}]({download}/{names[2]}) | [Download]({download}/{names[3]}) | [Download]({download}/{names[5]}) |
{kit_row}
Extract the ZIP and launch its ELF, or install the PSU with **PSU Paste**.
{kit_note}
For boot-path installs, [BOOT.ELF]({download}/{names[6]}) and [BOOT2.ELF]({download}/{names[7]}) are copies of the standard ELF.
Existing user settings are retained. The built-in updater preserves your controller version.

## Changelog

{changelog}

[Full changelog]({compare}) · [Documentation](https://{repo.split('/')[0].lower()}.github.io/{repo.split('/')[1]}/)

Device and display behavior still require console validation; a passing build does not establish hardware compatibility.

<details>
<summary>Build verification and SHA-256 checksums</summary>

Built from [{revision[:7]}]({base}/tree/{revision}). [Build checks]({workflow}) passed:
host regressions, documentation, standard/DS34 builds, controller variants,
loader memory layout and app-package verification. GitHub-computed SHA-256 digests and asset sizes are checked against the built files before the rolling tag moves; CI does not inflate download counts by downloading its own assets.

```text
{hashes}
```

</details>
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--previous", default="")
    parser.add_argument("--run-id", required=True, type=int)
    parser.add_argument("--assets-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version", default="",
                        help="build number that ends every published ZIP name, e.g. 284")
    parser.add_argument("--omit-kit", action="store_true", help="The optional kit was omitted from this publish retry")
    args = parser.parse_args()
    if args.version and not re.fullmatch(r"[0-9]+", args.version):
        parser.error("version must be a build number")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repo):
        parser.error("invalid repository")
    if not re.fullmatch(r"[0-9a-f]{40}", args.revision) or (
            args.previous and not re.fullmatch(r"[0-9a-f]{40}", args.previous)):
        parser.error("revision and previous must be full commit IDs")
    history = [f"{args.previous}..{args.revision}"] if args.previous else ["-n", "10", args.revision]
    log = subprocess.check_output(
        ["git", "log", "--first-parent", "--format=%H%x00%B%x1e", *history], encoding="utf-8")
    # Read the summary from the exact built revision, never a dirty worktree.
    changes = release_changes(args.repo, args.revision, args.previous, log)
    digests = {}
    for name in ASSETS:
        if args.omit_kit and name == "LAUNCHSTATION.zip":
            continue
        asset = args.assets_dir / name
        if not asset.is_file() or asset.stat().st_size == 0:
            if name in OPTIONAL_ASSETS:
                continue
            parser.error(f"missing or empty package: {asset}")
        with asset.open("rb") as stream:
            digests[name] = hashlib.file_digest(stream, "sha256").hexdigest()
    body = render(args.repo, args.revision, args.previous, args.run_id,
                  changes, digests, args.version, args.omit_kit)
    size = len(body.encode("utf-8"))
    if size > BODY_LIMIT:
        parser.error(f"release notes are {size} bytes, over the {BODY_LIMIT} byte limit the in-app update check relies on")
    args.output.write_text(body, encoding="utf-8")


if __name__ == "__main__":
    main()
