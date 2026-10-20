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
)


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


def release_changes(repo, revision, previous, log):
    current = revision_changelog(revision)
    # Keep useful notes on reruns, but never hide new commits behind an old
    # unchanged Rolling section carried over from the previous release.
    if current and (not previous or revision == previous or
                    current != revision_changelog(previous)):
        return current
    return changes_from_log(log, repo)


def render(repo, revision, previous, run_id, changes, digests):
    base = f"https://github.com/{repo}"
    compare = f"{base}/compare/{previous}...{revision}" if previous else f"{base}/commits/{revision}"
    workflow = f"{base}/actions/runs/{run_id}"
    download = f"{base}/releases/download/latest"
    changelog = "\n".join(changes) or "- Rebuild of the current revision; no new changes."
    hashes = "\n".join(f"{digests[name]}  {name}" for name in ASSETS)
    return f"""## Downloads

| Version | ZIP | PSU |
| --- | --- | --- |
| Standard | [Download]({download}/{ASSETS[0]}) | [Download]({download}/{ASSETS[1]}) |
| DS34 USB/Bluetooth controllers | [Download]({download}/{ASSETS[2]}) | [Download]({download}/{ASSETS[3]}) |

Extract the ZIP and launch its ELF, or install the PSU with **PSU Paste**.
Existing user settings are retained. The built-in updater preserves your controller version.

## Changelog

{changelog}

[Full changelog]({compare}) · [Documentation](https://{repo.split('/')[0].lower()}.github.io/{repo.split('/')[1]}/)

Device and display behavior still require console validation; a passing build does not establish hardware compatibility.

<details>
<summary>Build verification and SHA-256 checksums</summary>

Built from [{revision[:7]}]({base}/tree/{revision}). [Build checks]({workflow}) passed:
host regressions, documentation, standard/DS34 builds, controller variants,
loader memory layout and app-package verification. Published packages are downloaded and compared with the build outputs before the rolling tag moves.

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
    args = parser.parse_args()
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
        asset = args.assets_dir / name
        if not asset.is_file() or asset.stat().st_size == 0:
            parser.error(f"missing or empty package: {asset}")
        with asset.open("rb") as stream:
            digests[name] = hashlib.file_digest(stream, "sha256").hexdigest()
    args.output.write_text(render(args.repo, args.revision, args.previous, args.run_id,
                                  changes, digests), encoding="utf-8")


if __name__ == "__main__":
    main()
