"""Repository clutter inventory for one repo (evidence only).

Usage: python -I gitmeta.py <work_root> <Repo>
Outputs under evidence/<Repo>/ :
  git-refs.tsv        every remote branch and tag with last-commit date, ahead/behind vs default, merged flag
  repo-clutter.md     tracked-but-ignored files, build-output-like tracked files, largest tracked files,
                      binary totals per directory, suspicious names, empty files
"""
import os
import re
import subprocess
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402

BUILD_OUT = re.compile(r"\.(o|a|obj|elf|irx|erl|exe|dll|so|pyc|map|zip|7z|rar|xz|gz|bin|iso|img|kelf|psu)$", re.I)
SUSPECT = re.compile(r"(\.bak$|\.orig$|\.rej$|~$|\.tmp$|\.log$|\.swp$| - Copy|\(\d+\)\.|New folder|\.old$|_old\.|_backup|\.DS_Store|Thumbs\.db)", re.I)


def git(repo_dir, *args):
    return subprocess.run(["git", "-C", repo_dir] + list(args), capture_output=True, text=True,
                          encoding="utf-8", errors="replace").stdout


def main():
    root, repo = os.path.abspath(sys.argv[1]), sys.argv[2]
    src = os.path.join(root, "src", repo)
    out = os.path.join(root, "evidence", repo)
    os.makedirs(out, exist_ok=True)
    default = "origin/" + rules.DEFAULT_BRANCH[repo]

    # ---- refs
    rows = []
    fmt = "%(refname)\t%(creatordate:short)\t%(ahead-behind:" + default + ")\t%(subject)"
    for line in git(src, "for-each-ref", "--format=" + fmt, "refs/remotes/origin", "refs/tags").splitlines():
        ref, date, ab, subj = (line.split("\t") + ["", "", ""])[:4]
        if ref.endswith("/HEAD") or ref == "refs/remotes/origin":
            continue
        ahead, behind = (ab.split() + ["?", "?"])[:2]
        if ref.startswith("refs/tags/"):
            rows.append(("tag", ref[len("refs/tags/"):], date, ahead, behind,
                         "reachable from default" if ahead == "0" else "", subj[:80]))
            continue
        short = ref[len("refs/remotes/"):]
        if short == default:
            rows.append(("branch", short[len("origin/"):], date, "0", "0", "default", subj[:80]))
            continue
        rows.append(("branch", short[len("origin/"):], date, ahead, behind,
                     "merged" if ahead == "0" else "", subj[:80]))
    rows.sort(key=lambda r: (r[0], r[2]))
    with open(os.path.join(out, "git-refs.tsv"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("type\tname\tlast_commit\tahead_of_default\tbehind_default\tstate\tlast_subject\n")
        for r in rows:
            fh.write("\t".join(r) + "\n")

    # ---- clutter
    inv = []
    with open(os.path.join(root, "inventory", repo + ".tsv"), encoding="utf-8") as fh:
        next(fh)
        for line in fh:
            p, b, l, k = line.rstrip("\n").split("\t")
            inv.append((p, int(b), int(l), k))
    ignored = [l for l in git(src, "ls-files", "-ci", "--exclude-standard").splitlines() if l]
    md = ["# Repo clutter inventory: %s" % repo, "",
          "Mechanical listing only. Each item still needs a human-style judgement (some prebuilt binaries are shipped on purpose).", ""]
    md += ["## Totals", "",
           "- tracked files: %d, total %.1f MB" % (len(inv), sum(b for _, b, _, _ in inv) / 1048576.0),
           "- binary files: %d, %.1f MB" % (sum(1 for r in inv if r[3] == "binary"),
                                            sum(r[1] for r in inv if r[3] == "binary") / 1048576.0), ""]
    md += ["## Tracked files that match the repo's own .gitignore (%d)" % len(ignored), ""]
    md += ["- `%s`" % p for p in ignored[:200]] or ["- none"]
    md += ["", "## Tracked files that look like build output or archives", "",
           "| bytes | path |", "|---:|---|"]
    bo = sorted(((b, p) for p, b, _, _ in inv if BUILD_OUT.search(p)), reverse=True)
    md += ["| %d | `%s` |" % x for x in bo[:300]]
    md += ["", "Total: %d files, %.1f MB" % (len(bo), sum(b for b, _ in bo) / 1048576.0), ""]
    md += ["## Largest tracked files (top 60)", "", "| bytes | kind | path |", "|---:|---|---|"]
    for p, b, l, k in sorted(inv, key=lambda r: -r[1])[:60]:
        md.append("| %d | %s | `%s` |" % (b, k, p))
    per = defaultdict(lambda: [0, 0])
    for p, b, l, k in inv:
        if k == "binary":
            parts = p.split("/")
            key = "/".join(parts[:2]) if len(parts) > 2 else (parts[0] if len(parts) > 1 else "(root)")
            per[key][0] += 1
            per[key][1] += b
    md += ["", "## Binary bytes per directory (depth 2)", "", "| files | bytes | directory |", "|---:|---:|---|"]
    for key, (n, b) in sorted(per.items(), key=lambda kv: -kv[1][1])[:60]:
        md.append("| %d | %d | `%s` |" % (n, b, key))
    sus = [p for p, _, _, _ in inv if SUSPECT.search(p)]
    md += ["", "## Suspicious names (backup/temp/copy patterns) (%d)" % len(sus), ""]
    md += ["- `%s`" % p for p in sus[:200]] or ["- none"]
    empty = [p for p, b, _, _ in inv if b == 0]
    md += ["", "## Empty tracked files (%d)" % len(empty), ""]
    md += ["- `%s`" % p for p in empty[:200]] or ["- none"]
    with open(os.path.join(out, "repo-clutter.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(md) + "\n")
    nb = sum(1 for r in rows if r[0] == "branch")
    print("%s: branches=%d (merged %d) tags=%d ignored-but-tracked=%d build-output-like=%d suspicious=%d empty=%d" % (
        repo, nb, sum(1 for r in rows if r[5] == "merged"), sum(1 for r in rows if r[0] == "tag"),
        len(ignored), len(bo), len(sus), len(empty)))


if __name__ == "__main__":
    main()
