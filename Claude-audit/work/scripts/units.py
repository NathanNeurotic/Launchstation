"""Turn the chunk plan into reading units (chunk x lens) and split them into workflow parts.

Usage: python -I units.py <work_root>
Writes plan/<Repo>.units.json and plan/args/<Repo>-<part>.json (the args object for one workflow).

Unit id = chunk id for the first pass, chunk id + ".b" for the second (defects) pass on core/module chunks.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402

PARTS = {"RiptOPL": 4, "Launchstation": 3, "Simple-Media-System": 2, "PS2-Servers": 1, "POPSLoader": 1}
TIER_PRIO = {"core": 0, "module": 1, "codec": 2, "buildci": 3, "support": 4, "docs": 5, "skim": 6}
TWO_LENS = ("core", "module")


def lens_of(unit_id, tier):
    if unit_id.endswith(".b"):
        return "defects"
    return "structure" if tier in TWO_LENS else tier


def main():
    root = os.path.abspath(sys.argv[1])
    os.makedirs(os.path.join(root, "plan", "args"), exist_ok=True)
    for repo in rules.REPOS:
        with open(os.path.join(root, "plan", repo + ".json"), encoding="utf-8") as fh:
            plan = json.load(fh)
        units = []
        for order, ch in enumerate(plan["chunks"]):
            ids = [ch["id"]] + ([ch["id"] + ".b"] if ch["tier"] in TWO_LENS else [])
            for uid in ids:
                units.append({"id": uid, "chunk": ch["id"], "tier": ch["tier"], "lens": lens_of(uid, ch["tier"]),
                              "lines": ch["lines"], "nfiles": len(ch["files"]), "label": ch["label"], "order": order})
        with open(os.path.join(root, "plan", repo + ".units.json"), "w", encoding="utf-8", newline="\n") as fh:
            json.dump(units, fh, indent=1)
        nparts = PARTS[repo]
        parts = [[] for _ in range(nparts)]
        cost = [0.0] * nparts
        for u in sorted(units, key=lambda u: (TIER_PRIO[u["tier"]], u["order"], u["id"])):
            i = min(range(nparts), key=lambda k: cost[k])
            parts[i].append(u)
            weight = {"defects": 1.5, "structure": 1.0, "codec": 0.8, "buildci": 1.0, "support": 0.5, "docs": 0.3}
            cost[i] += (4000 if u["lens"] == "skim" else u["lines"] * weight[u["lens"]]) + 1500
        for i, p in enumerate(parts):
            name = "ABCDEFGH"[i]
            arg = {"repo": repo, "part": name,
                   "units": ["%s:%s:%d:%d" % (u["id"], u["tier"], u["lines"], u["nfiles"]) for u in p]}
            with open(os.path.join(root, "plan", "args", "%s-%s.json" % (repo, name)), "w", encoding="utf-8",
                      newline="\n") as fh:
                json.dump(arg, fh, separators=(",", ":"))
            print("%s part %s: %d units, cost %.0f" % (repo, name, len(p), cost[i]))
        print("%s: %d units from %d chunks" % (repo, len(units), len(plan["chunks"])))


if __name__ == "__main__":
    main()
