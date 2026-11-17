"""Copy one repo's audit work state into <pr_checkout>/Claude-audit/work, so the audit can resume from the PR branch alone.

Usage: python -I sync_to_pr.py <work_root> <Repo> [<pr_checkout>]      (default checkout: <work_root>/pr/<Repo>)
In text files, the local work root becomes <WORK_ROOT> and the home folder <HOME>; e-mail addresses become <email>;
secret-like strings are replaced by <redacted>, except hex strings that do not occur in the audited sources
(commit ids and content hashes computed by the audit).
"""
import json
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate_findings as vf  # noqa: E402

SHARED = ["AUDIT-RULES.md", "FINDINGS-FORMAT.md", "RESUME.md", "prompts", "scripts", "workflows"]
PER_REPO = ["findings/{r}", "tmp/{r}", "leads/{r}", "evidence/{r}", "evidence/_cross", "brief/{r}.md",
            "inventory/{r}.tsv", "plan/{r}.json", "plan/{r}.units.json"]
SKIP = {"__pycache__", "STOP_PUBLISH", "publish.log"}
KEEP_IN_DEST = {"src", "pr"}
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")
# Re-include everything first: the repo's own .gitignore (for example tmp/ or *.log) must not drop audit files.
GITIGNORE = "!*\n!*/\nsrc/\npr/\n__pycache__/\ntmp/STOP_PUBLISH\ntmp/publish.log\n"
SETUP = """#!/bin/sh
# Run once after checking out the claude-audit-findings branch (cloud or fresh clone).
# Creates src/{repo} next to this script: the audited commit as a detached git worktree (ignored by git).
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(git -C "$here" rev-parse --show-toplevel)
sha={sha}
if [ ! -e "$here/src/{repo}" ]; then
  git -C "$top" cat-file -e "$sha^{{commit}}" 2>/dev/null || git -C "$top" fetch --quiet origin "$sha" || git -C "$top" fetch --quiet --unshallow origin
  git -C "$top" worktree add --quiet --detach "$here/src/{repo}" "$sha"
fi
echo "src/{repo} at $(git -C "$here/src/{repo}" rev-parse HEAD)"
"""


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")


def path_forms(p):
    p = os.path.abspath(p).rstrip("\\/")
    fwd = p.replace("\\", "/")
    forms = {p, fwd, p.replace("\\", "\\\\")}
    if re.match(r"^[A-Za-z]:/", fwd):
        forms.add("/" + fwd[0].lower() + fwd[2:])
    return sorted(forms, key=len, reverse=True)


def source_secrets(src):
    found = set()
    files = run(["git", "-C", src, "ls-files", "-z"]).stdout.split("\0")
    for f in files:
        fp = os.path.join(src, f)
        if not f or not os.path.isfile(fp) or os.path.getsize(fp) > 8 << 20:
            continue
        with open(fp, "rb") as fh:
            b = fh.read()
        if b"\0" in b[:8192]:
            continue
        t = b.decode("utf-8", "replace")
        for m in vf.SECRET[0].finditer(t):
            found.add(m.group(0))
    return found


def clean(text, roots, homes, src_hex, counts):
    for f in roots:
        text = re.sub(re.escape(f), "<WORK_ROOT>", text, flags=re.I)
    for f in homes:
        text = re.sub(re.escape(f), "<HOME>", text, flags=re.I)
    text, n = EMAIL.subn("<email>", text)
    counts["email"] += n

    def hexsub(m):
        if m.group(0) in src_hex:
            counts["secret"] += 1
            return "<redacted>"
        return m.group(0)
    text = vf.SECRET[0].sub(hexsub, text)
    for rx in vf.SECRET[1:]:
        text, n = rx.subn("<redacted>", text)
        counts["secret"] += n
    return text


def sync(root, repo, checkout=None):
    root = os.path.abspath(root)
    checkout = os.path.abspath(checkout or os.path.join(root, "pr", repo))
    src = os.path.join(root, "src", repo)
    sha = run(["git", "-C", src, "rev-parse", "HEAD"]).stdout.strip()
    if len(sha) != 40:
        print("%s: cannot read the audited commit from %s" % (repo, src))
        return False
    dest = os.path.join(checkout, "Claude-audit", "work")
    os.makedirs(dest, exist_ok=True)
    for name in os.listdir(dest):
        if name not in KEEP_IN_DEST:
            p = os.path.join(dest, name)
            shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
    roots = path_forms(root)
    homes = path_forms(os.path.expanduser("~"))
    src_hex = source_secrets(src)
    counts = {"email": 0, "secret": 0, "files": 0, "bytes": 0}
    touched, binaries = [], []
    items = SHARED + [p.format(r=repo) for p in PER_REPO]
    for item in items:
        base = os.path.join(root, item)
        if not os.path.exists(base):
            continue
        walk = [(os.path.dirname(base), [], [os.path.basename(base)])] if os.path.isfile(base) else os.walk(base)
        for d, dirs, files in walk:
            dirs[:] = [x for x in dirs if x not in SKIP]
            for f in files:
                if f in SKIP:
                    continue
                sp = os.path.join(d, f)
                rel = os.path.relpath(sp, root)
                dp = os.path.join(dest, rel)
                os.makedirs(os.path.dirname(dp), exist_ok=True)
                with open(sp, "rb") as fh:
                    b = fh.read()
                if b"\0" in b[:8192]:
                    binaries.append(rel)
                    continue
                before = dict(counts)
                t = clean(b.decode("utf-8", "replace"), roots, homes, src_hex, counts)
                if counts["secret"] != before["secret"] or counts["email"] != before["email"]:
                    touched.append(rel)
                if f.endswith(".json"):
                    try:
                        json.loads(t)
                    except ValueError as e:
                        print("%s: %s is not valid JSON after cleaning (%s); stopping" % (repo, rel, e))
                        return False
                with open(dp, "w", encoding="utf-8", newline="") as fh:
                    fh.write(t)
                counts["files"] += 1
                counts["bytes"] += len(t.encode("utf-8"))
    with open(os.path.join(dest, ".gitignore"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(GITIGNORE)
    with open(os.path.join(dest, "setup.sh"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(SETUP.format(repo=repo, sha=sha))
    leak = []
    for d, dirs, files in os.walk(dest):
        dirs[:] = [x for x in dirs if x not in KEEP_IN_DEST]
        for f in files:
            with open(os.path.join(d, f), encoding="utf-8", errors="replace") as fh:
                t = fh.read()
            if any(h.lower() in t.lower() for h in homes) or EMAIL.search(t):
                leak.append(os.path.relpath(os.path.join(d, f), dest))
    if leak:
        print("%s: local path or e-mail left in %s; stopping" % (repo, leak))
        return False
    print("%s: work state synced: %d files, %.1f MB; %d e-mails and %d secret-like strings redacted%s%s" % (
        repo, counts["files"], counts["bytes"] / 1e6, counts["email"], counts["secret"],
        ("; redacted in: " + ", ".join(touched[:12]) + (" ..." if len(touched) > 12 else "")) if touched else "",
        ("; binary files not copied: " + ", ".join(binaries)) if binaries else ""))
    return True


if __name__ == "__main__":
    ok = sync(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    sys.exit(0 if ok else 1)
