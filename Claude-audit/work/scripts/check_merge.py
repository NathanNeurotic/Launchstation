"""Check findings/<Repo>/_merge.json.

Usage: python -I check_merge.py <work_root> <Repo>
"""
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate_findings as vf  # noqa: E402

ALLOWED = {"title", "category", "locations", "problem", "fix", "justification", "benefit", "benefit_rank",
           "lines_removed", "effort", "risk", "confidence", "basis"}


def surviving_keys(root, repo):
    keys = set()
    fdir = os.path.join(root, "findings", repo)
    for path in glob.glob(os.path.join(fdir, "*.json")):
        base = os.path.basename(path)
        if base.startswith("_") or base.endswith(".verify.json"):
            continue
        unit = base[:-5]
        with open(path, encoding="utf-8") as fh:
            d = json.load(fh)
        verdicts = {}
        vp = os.path.join(fdir, unit + ".verify.json")
        if os.path.isfile(vp):
            with open(vp, encoding="utf-8") as fh:
                verdicts = {x.get("n"): x.get("verdict") for x in json.load(fh).get("verdicts", [])}
        for f in d.get("findings", []):
            if verdicts.get(f.get("n")) not in (None, "refuted"):
                keys.add("%s#%s" % (unit, f.get("n")))
    return keys


def main():
    root, repo = os.path.abspath(sys.argv[1]), sys.argv[2]
    path = os.path.join(root, "findings", repo, "_merge.json")
    try:
        with open(path, encoding="utf-8") as fh:
            m = json.load(fh)
    except Exception as e:  # noqa: BLE001
        print("ERROR: cannot parse %s: %s" % (path, e))
        sys.exit(1)
    keys = surviving_keys(root, repo)
    errs, used = [], {}
    for i, g in enumerate(m.get("groups", []) or []):
        where = "group %d" % (i + 1)
        if not isinstance(g, dict):
            errs.append(where + ": must be an object")
            continue
        members = [g.get("keep")] + list(g.get("absorb", []) or [])
        for k in members:
            if k not in keys:
                errs.append("%s: %r is not a verified, surviving finding key" % (where, k))
            elif k in used:
                errs.append("%s: %r already used in %s" % (where, k, used[k]))
            else:
                used[k] = where
        merged = g.get("merged") or {}
        if not isinstance(merged, dict):
            errs.append(where + ": merged must be an object")
            continue
        bad = set(merged) - ALLOWED
        if bad:
            errs.append("%s: fields not allowed in merged: %s" % (where, sorted(bad)))
        vf.check_text(merged, where + " merged", errs, required=())
        vf.check_enums(merged, where + " merged", errs, partial=True)
        if "locations" in merged:
            vf.check_locations(root, repo, merged["locations"], where + " merged", errs)
    for i, d in enumerate(m.get("drop", []) or []):
        k = d.get("key") if isinstance(d, dict) else None
        if k not in keys:
            errs.append("drop %d: %r is not a verified, surviving finding key" % (i + 1, k))
        elif k in used:
            errs.append("drop %d: %r is also in %s" % (i + 1, k, used[k]))
        elif not str(d.get("reason", "")).strip():
            errs.append("drop %d: reason is required" % (i + 1))
        else:
            used[k] = "drop"
    if errs:
        print("INVALID (%d problems):" % len(errs))
        for e in errs:
            print(" - " + e)
        sys.exit(1)
    print("OK: %s merge: %d groups, %d drops" % (repo, len(m.get("groups", []) or []), len(m.get("drop", []) or [])))


if __name__ == "__main__":
    main()
