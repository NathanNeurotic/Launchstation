"""Progress of the reading and verification passes, from the files on disk.

Usage: python -I status.py <work_root> [Repo ...] [--missing]
"""
import json
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402


def load(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except Exception:  # noqa: BLE001
        return None


def main():
    root = os.path.abspath(sys.argv[1])
    repos = [a for a in sys.argv[2:] if not a.startswith("--")] or rules.REPOS
    show_missing = "--missing" in sys.argv
    for repo in repos:
        units = load(os.path.join(root, "plan", repo + ".units.json")) or []
        fdir = os.path.join(root, "findings", repo)
        tdir = os.path.join(root, "tmp", repo)
        read = verified = raised = notes = 0
        verdicts = Counter()
        by_rank, by_cat = Counter(), Counter()
        missing, unverified, broken = [], [], []
        for u in units:
            fp = os.path.join(fdir, u["id"] + ".json")
            if os.path.isfile(fp):
                d = load(fp)
                if not isinstance(d, dict) or not isinstance(d.get("findings"), list):
                    broken.append(u["id"])
                    continue
                read += 1
                n = len(d["findings"])
                raised += n
                for f in d["findings"]:
                    by_rank[f.get("benefit_rank")] += 1
                    by_cat[f.get("category")] += 1
                vp = os.path.join(fdir, u["id"] + ".verify.json")
                v = load(vp) if os.path.isfile(vp) else None
                if isinstance(v, dict) and isinstance(v.get("verdicts"), list):
                    verified += 1
                    for x in v["verdicts"]:
                        verdicts[x.get("verdict")] += 1
                elif n == 0:
                    verified += 1
                else:
                    unverified.append(u["id"])
            else:
                missing.append(u["id"])
                if os.path.isfile(os.path.join(tdir, u["id"] + ".notes.md")):
                    notes += 1
        sweeps = [f for f in (os.listdir(fdir) if os.path.isdir(fdir) else [])
                  if f.startswith("sweep-") and f.endswith(".json") and not f.endswith(".verify.json")]
        print("%-20s units %3d | read %3d | verified %3d | in progress (notes) %3d | not started %3d | broken %d | sweeps %d" % (
            repo, len(units), read, verified, notes, len(missing) - notes, len(broken), len(sweeps)))
        print("%-20s findings raised %d | verdicts: confirmed %d, corrected %d, refuted %d | rank %s | cat %s" % (
            "", raised, verdicts["confirmed"], verdicts["corrected"], verdicts["refuted"],
            " ".join("%s:%d" % (k, by_rank[k]) for k in sorted(by_rank, key=str, reverse=True)),
            " ".join("%s:%d" % (k, by_cat[k]) for k in sorted(by_cat, key=str))))
        if show_missing:
            if missing:
                print("   not read: " + " ".join(missing))
            if unverified:
                print("   read, not verified: " + " ".join(unverified))
            if broken:
                print("   broken: " + " ".join(broken))


if __name__ == "__main__":
    main()
