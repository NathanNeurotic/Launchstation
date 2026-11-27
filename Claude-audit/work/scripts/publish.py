"""Render the report for each repo into its PR worktree, then commit and push if it changed.

Usage: python -I publish.py <work_root> <Repo> [<Repo> ...] [--final]
Interim reports show verified findings only. Commits carry [skip ci] so no workflow runs.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate_findings  # noqa: E402

ALLOWED = {"RiptOPL", "Launchstation"}


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", **kw)


def main():
    root = os.path.abspath(sys.argv[1])
    final = "--final" in sys.argv
    repos = [a for a in sys.argv[2:] if not a.startswith("--")]
    for repo in repos:
        if repo not in ALLOWED:
            print("%s: not in this week's scope, skipped" % repo)
            continue
        wt = os.path.join(root, "pr", repo)
        local = os.path.isdir(wt)
        if not local:  # cloud layout: the work root is Claude-audit/work inside the PR checkout itself
            wt = run(["git", "-C", root, "rev-parse", "--show-toplevel"]).stdout.strip()
        out = os.path.join(wt, "Claude-audit", "Claude-%s.md" % repo)
        r = run([sys.executable, "-I", os.path.join(root, "scripts", "render.py"), root, repo, out]
                + (["--final"] if final else [] if "--with-unverified" in sys.argv else ["--verified-only"]))
        print(r.stdout.strip() or r.stderr.strip())
        if r.returncode:
            print("%s: render failed, nothing committed" % repo)
            continue
        with open(out, encoding="utf-8") as fh:
            text = fh.read()
        sha = run(["git", "-C", os.path.join(root, "src", repo), "rev-parse", "HEAD"]).stdout.strip()
        top = run(["git", "-C", os.path.join(root, "src", repo), "rev-parse", "--show-toplevel"]).stdout.strip()
        if not (top and os.path.isdir(top) and os.path.samefile(top, os.path.join(root, "src", repo))):
            print("%s: src/%s is not its own checkout (run setup.sh); NOT committed" % (repo, repo))
            continue
        scan = text.replace(sha, "") if len(sha) == 40 else text
        if any(rx.search(scan) for rx in validate_findings.SECRET):
            print("%s: rendered report contains a secret-like string; NOT committed" % repo)
            continue
        branch = run(["git", "-C", wt, "rev-parse", "--abbrev-ref", "HEAD"]).stdout.strip()
        if branch != "claude-audit-findings":
            print("%s: worktree is on %r, expected claude-audit-findings; NOT committed" % (repo, branch))
            continue
        if local:
            import sync_to_pr  # noqa: E402
            if not sync_to_pr.sync(root, repo, wt):
                print("%s: work-state sync failed; NOT committed" % repo)
                continue
        run(["git", "-C", wt, "add", "-A", "--", "Claude-audit"])
        if run(["git", "-C", wt, "diff", "--cached", "--quiet"]).returncode == 0:
            print("%s: no change" % repo)
            continue
        other = run(["git", "-C", wt, "diff", "--cached", "--name-only"]).stdout.split()
        if any(not p.startswith("Claude-audit/") for p in other):
            print("%s: staged files outside Claude-audit/: %s; NOT committed" % (repo, other))
            continue
        first = text.splitlines()[2] if len(text.splitlines()) > 2 else ""
        msg = "docs: %s Claude-audit report for %s [skip ci]\n\n%s" % ("finalize" if final else "update", repo, first)
        c = run(["git", "-C", wt, "commit", "-q", "-m", msg])
        if c.returncode:
            print("%s: commit failed: %s" % (repo, c.stderr.strip()))
            continue
        p = run(["git", "-C", wt, "push", "-q", "origin", "claude-audit-findings"])
        print("%s: committed and %s" % (repo, "pushed" if p.returncode == 0 else "PUSH FAILED: " + p.stderr.strip()))


if __name__ == "__main__":
    main()
