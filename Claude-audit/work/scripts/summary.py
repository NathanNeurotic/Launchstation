"""Build Claude-SUMMARY.md from findings/<Repo>/_final.json (run render.py for each repo first).

Usage: python -I summary.py <work_root> <out_md> [--order <order_md>]
The suggested order of work is hand-written in <order_md> and pasted in verbatim.
"""
import json
import os
import sys
from collections import Counter

REPOS = ["RiptOPL", "Launchstation"]
DEFERRED = ["POPSLoader", "Simple-Media-System", "PS2-Servers"]
CONF = {"High": 0, "Med": 1, "Low": 2}
EFF = {"S": 0, "M": 1, "L": 2}


def one(s):
    return " ".join(str(s or "").split())


def key(f):
    return (-int(f.get("benefit_rank", 1)), -int(f.get("lines_removed", 0) or 0),
            CONF.get(f.get("confidence"), 3), EFF.get(f.get("effort"), 3), one(f.get("title")))


def row(f):
    return "| %s | %s | %s | %s | %s/%s | %s |" % (
        f["id"], one(f.get("title")).replace("|", "/"), f.get("category"), f.get("benefit_rank"),
        f.get("effort"), f.get("risk"), one(f.get("benefit")).replace("|", "/"))


def main():
    root, out = os.path.abspath(sys.argv[1]), sys.argv[2]
    order = None
    if "--order" in sys.argv:
        with open(sys.argv[sys.argv.index("--order") + 1], encoding="utf-8") as fh:
            order = fh.read().strip()
    data, allf = {}, []
    for r in REPOS:
        with open(os.path.join(root, "findings", r, "_final.json"), encoding="utf-8") as fh:
            data[r] = json.load(fh)
        for f in data[r]["findings"]:
            if f.get("verified"):
                allf.append(dict(f, repo=r))
    L = ["# Claude audit: summary", ""]
    final = all(data[r].get("final") for r in REPOS)
    L.append("Status: %s" % ("COMPLETE for RiptOPL and Launchstation." if final else
                             "IN PROGRESS. Numbers and IDs change until both repo reports say COMPLETE."))
    L += ["", "Scope this week: RiptOPL and Launchstation only. %s were not audited this week (their draft PRs hold only a skeleton report)." % ", ".join(DEFERRED), ""]
    for r in REPOS:
        L.append("- %s at `%s`: `Claude-audit/Claude-%s.md`" % (r, data[r]["commit"], r))
    L += ["", "## Totals", "", "| Repo | Findings | rank 5 | rank 4 | rank 3 | rank 2 | rank 1 | Lines removable (sum of estimates) |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for r in REPOS:
        fs = [f for f in allf if f["repo"] == r]
        c = Counter(int(f.get("benefit_rank", 1)) for f in fs)
        L.append("| %s | %d | %s | %d |" % (r, len(fs), " | ".join(str(c.get(k, 0)) for k in (5, 4, 3, 2, 1)),
                                           sum(int(f.get("lines_removed", 0) or 0) for f in fs)))
    L += ["", "Line counts are the readers' estimates after re-check; they overlap where one fix covers several findings, so treat the sum as an upper bound.", ""]

    L += ["## Problems shared by both repos (listed once)", ""]
    shared = [f for f in allf if any(l.get("repo") and l.get("repo") != f["repo"] for l in f.get("locations", []))]
    if shared:
        L += ["| ID | Title | Other repo's locations |", "|---|---|---|"]
        for f in sorted(shared, key=key):
            other = sorted({"%s/%s" % (l["repo"], l["path"]) for l in f["locations"] if l.get("repo") and l["repo"] != f["repo"]})
            L.append("| %s | %s | %s |" % (f["id"], one(f.get("title")).replace("|", "/"), "; ".join("`%s`" % o for o in other)))
    else:
        L.append("None found so far.")
    L += ["", "## Top 25 (both repos, by benefit)", "", "| ID | Title | Cat | Rank | Effort/Risk | Benefit |", "|---|---|---:|---:|---|---|"]
    L += [row(f) for f in sorted(allf, key=key)[:25]]
    qw = sorted((f for f in allf if f.get("risk") == "Low" and f.get("effort") == "S"), key=key)
    L += ["", "## Quick wins (Low risk, S effort): %d" % len(qw), "", "| ID | Title | Cat | Rank | Effort/Risk | Benefit |", "|---|---|---:|---:|---|---|"]
    L += [row(f) for f in qw]
    L += ["", "## Suggested order of work", "", order or "(written when both reports are complete)", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")
    print("summary: %d findings, %d shared, %d quick wins -> %s" % (len(allf), len(shared), len(qw), out))


if __name__ == "__main__":
    main()
