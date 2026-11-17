"""Compact digest of one repo's findings so far (for sweeps, merging and the completeness check).

Usage: python -I digest.py <work_root> <Repo> [findings|helpers|summaries|coverage] [--all]
  findings   one line per surviving finding: key | cat | rank | lines_removed | title | locations   (default)
  helpers    reusable helpers reported by readers
  summaries  module summaries per unit
  coverage   files that were not read in full, with the reader's note
--all includes findings that are still unverified (marked [unverified]); refuted findings are never shown.
"""
import glob
import json
import os
import sys


def load(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except Exception:  # noqa: BLE001
        return None


def main():
    root, repo = os.path.abspath(sys.argv[1]), sys.argv[2]
    mode = next((a for a in sys.argv[3:] if not a.startswith("--")), "findings")
    show_all = "--all" in sys.argv
    fdir = os.path.join(root, "findings", repo)
    for path in sorted(glob.glob(os.path.join(fdir, "*.json"))):
        base = os.path.basename(path)
        if base.startswith("_") or base.endswith(".verify.json"):
            continue
        unit = base[:-5]
        d = load(path)
        if not isinstance(d, dict):
            continue
        if mode == "summaries":
            print("[%s] %s" % (unit, " ".join(str(d.get("module_summary", "")).split())))
            continue
        if mode == "helpers":
            for h in d.get("reusable_helpers", []) or []:
                if isinstance(h, dict):
                    print("%s:%s  %s  -- %s  [%s]" % (h.get("path"), h.get("line"), h.get("name"),
                                                     " ".join(str(h.get("what", "")).split()), unit))
            continue
        if mode == "coverage":
            for c in d.get("coverage", []) or []:
                if isinstance(c, dict) and c.get("status") != "read_full":
                    print("%s  %s  %s  [%s]" % (c.get("path"), c.get("status"), " ".join(str(c.get("note", "")).split()), unit))
            continue
        v = load(os.path.join(fdir, unit + ".verify.json"))
        verdicts = {x.get("n"): x for x in (v.get("verdicts", []) if isinstance(v, dict) else []) if isinstance(x, dict)}
        for f in d.get("findings", []) or []:
            if not isinstance(f, dict):
                continue
            vd = verdicts.get(f.get("n"))
            if vd is None and not show_all:
                continue
            if vd is not None and vd.get("verdict") == "refuted":
                continue
            f = dict(f)
            if vd is not None and vd.get("verdict") == "corrected" and isinstance(vd.get("corrected"), dict):
                f.update(vd["corrected"])
            locs = []
            for l in f.get("locations", []) or []:
                r = (l.get("repo") + "/") if l.get("repo") and l.get("repo") != repo else ""
                locs.append("%s%s%s" % (r, l.get("path"), "" if l.get("whole") or "start" not in l else ":%s-%s" % (l["start"], l["end"])))
            print("%s#%s | c%s r%s -%s | %s%s | %s" % (unit, f.get("n"), f.get("category"), f.get("benefit_rank"),
                                                      f.get("lines_removed"), "" if vd else "[unverified] ",
                                                      " ".join(str(f.get("title", "")).split()), "; ".join(locs)))


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:  # noqa: BLE001
        pass
    main()
