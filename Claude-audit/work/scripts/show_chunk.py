"""Print the assignment of one reading unit.

Usage: python -I show_chunk.py <work_root> <Repo> <unit-id>
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402


def main():
    root, repo, unit = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]
    if repo not in rules.REPOS:
        print("ERROR: unknown repo %r (one of %s)" % (repo, ", ".join(rules.REPOS)))
        sys.exit(1)
    with open(os.path.join(root, "plan", repo + ".units.json"), encoding="utf-8") as fh:
        units = {u["id"]: u for u in json.load(fh)}
    if unit not in units:
        print("ERROR: unknown unit %r for %s" % (unit, repo))
        sys.exit(1)
    u = units[unit]
    with open(os.path.join(root, "plan", repo + ".json"), encoding="utf-8") as fh:
        plan = json.load(fh)
    ch = next(c for c in plan["chunks"] if c["id"] == u["chunk"])
    print("UNIT %s   repo %s   tier %s   lens %s" % (unit, repo, u["tier"], u["lens"]))
    print("Audited commit: %s (branch %s)" % (plan["commit"][:10], rules.DEFAULT_BRANCH[repo]))
    print("Source root: src/%s/   (paths below are relative to it)" % repo)
    print()
    if ch["tier"] == "skim":
        print("FILES (structural review of this set, %d files, %d lines: %s):" % (len(ch["files"]), ch["lines"], ch["label"]))
    else:
        print("FILES (read every line of every range, in this order):")
    for f in ch["files"]:
        if f.get("part"):
            print("  %-70s lines %d-%d   PART of the file; other readers have the rest" % (f["path"], f["start"], f["end"]))
        else:
            print("  %-70s lines 1-%d" % (f["path"], f["lines"]))
    print("TOTAL: %d lines in %d files" % (ch["lines"], len(ch["files"])))
    print()
    print("OUTPUT:   findings/%s/%s.json   (\"repo\": \"%s\", \"chunk\": \"%s\")" % (repo, unit, repo, unit))
    print("NOTES:    tmp/%s/%s.notes.md" % (repo, unit))
    print("VALIDATE: python -I scripts/validate_findings.py . %s %s" % (repo, unit))
    print("COVERAGE: one entry per file listed above (%d entries)" % len({f["path"] for f in ch["files"]}))
    be = os.path.join(root, "evidence", repo, "BUILD-EVIDENCE.md")
    print("BUILD EVIDENCE: evidence/%s/BUILD-EVIDENCE.md %s; raw data in evidence/%s/raw/" % (
        repo, "(present)" if os.path.isfile(be) else "(not written yet)", repo))
    print()
    lp = os.path.join(root, "leads", repo, u["chunk"] + ".md")
    if ch["tier"] != "skim" and os.path.isfile(lp):
        print("LEADS from scripts (hints only, UNVERIFIED, often wrong; check each one yourself):")
        print()
        with open(lp, encoding="utf-8") as fh:
            sys.stdout.write(fh.read())
    else:
        print("LEADS: none from the scripts for this unit.")


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:  # noqa: BLE001
        pass
    main()
