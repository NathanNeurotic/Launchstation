"""Validate a findings file or a verification file.

Usage:
  python -I validate_findings.py <work_root> <Repo> <chunk-id>            # checks findings/<Repo>/<chunk-id>.json
  python -I validate_findings.py <work_root> <Repo> <chunk-id> --verify   # checks findings/<Repo>/<chunk-id>.verify.json

Exit code 0 = valid. Otherwise prints every problem; fix the file and run again.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402

CAT = {1, 2, 3, 4, 5, 6, 7, 8}
LIMITS = {"title": 150, "problem": 800, "fix": 800, "justification": 600, "benefit": 320, "evidence": 1200}
SECRET = [
    re.compile(r"\b[A-Fa-f0-9]{32,}\b"),
    re.compile(r"\b(?=[A-Za-z0-9+]*[0-9])(?=[A-Za-z0-9+]*[a-z])(?=[A-Za-z0-9+]*[A-Z])[A-Za-z0-9+]{40,}={0,2}"),
    re.compile(r"gh[pousr]_[A-Za-z0-9]{20,}"),
    re.compile(r"-----BEGIN [A-Z ]*KEY"),
    re.compile(r"\bAKIA[0-9A-Z]{16}\b"),
    re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}"),
]
_LINES = {}


def line_count(root, repo, path):
    key = (repo, path)
    if key not in _LINES:
        full = os.path.join(root, "src", repo, path)
        if os.path.isdir(full):
            _LINES[key] = -2
        elif not os.path.isfile(full):
            _LINES[key] = -1
        else:
            with open(full, "rb") as fh:
                data = fh.read()
            _LINES[key] = data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)
    return _LINES[key]


def check_locations(root, repo, locs, where, errs):
    if not isinstance(locs, list) or not locs:
        errs.append("%s: locations must be a non-empty list" % where)
        return
    for i, loc in enumerate(locs):
        w = "%s location %d" % (where, i + 1)
        if not isinstance(loc, dict) or "path" not in loc:
            errs.append(w + ": needs an object with 'path'")
            continue
        r = loc.get("repo", repo)
        if r not in rules.REPOS:
            errs.append(w + ": unknown repo %r" % r)
            continue
        p = loc["path"]
        if "\\" in p or p.startswith("/") or p.startswith("src/" + r) or re.match(r"^[A-Za-z]:", p):
            errs.append(w + ": path must be repo-relative with forward slashes, got %r" % p)
            continue
        n = line_count(root, r, p)
        if n == -1:
            errs.append(w + ": path does not exist in %s: %s" % (r, p))
            continue
        if loc.get("whole"):
            continue
        if n == -2:
            errs.append(w + ": %s is a directory; use \"whole\": true" % p)
            continue
        s, e = loc.get("start"), loc.get("end")
        if not isinstance(s, int) or not isinstance(e, int):
            errs.append(w + ": needs integer start and end (or \"whole\": true for a whole file/dir/binary)")
            continue
        if s < 1 or e < s or e > n:
            errs.append(w + ": lines %s-%s out of range for %s (file has %d lines)" % (s, e, p, n))


def check_text(obj, where, errs, required=("title", "problem", "fix", "benefit")):
    for k in required:
        if not isinstance(obj.get(k), str) or not obj[k].strip():
            errs.append("%s: missing text field %r" % (where, k))
    for k, lim in LIMITS.items():
        v = obj.get(k)
        if isinstance(v, str):
            if len(v) > lim:
                errs.append("%s: %s is %d chars, limit %d (tighten it)" % (where, k, len(v), lim))
            for rx in SECRET:
                if rx.search(v):
                    errs.append("%s: %s contains a secret-like or hash-like string; never quote keys/tokens/hashes "
                                "(say 'secret-like value at <path>'; shorten commit ids to 10 chars)" % (where, k))
                    break


def check_enums(obj, where, errs, partial=False):
    def chk(k, allowed):
        if k in obj:
            if obj[k] not in allowed:
                errs.append("%s: %s must be one of %s, got %r" % (where, k, sorted(allowed, key=str), obj[k]))
        elif not partial:
            errs.append("%s: missing %s" % (where, k))
    chk("category", CAT)
    chk("benefit_rank", {1, 2, 3, 4, 5})
    chk("effort", {"S", "M", "L"})
    chk("risk", {"Low", "Med", "High"})
    chk("confidence", {"High", "Med", "Low"})
    chk("basis", {"READ", "INFERRED"})
    if "lines_removed" in obj and not isinstance(obj["lines_removed"], int):
        errs.append("%s: lines_removed must be an integer (0 if not applicable)" % where)
    elif "lines_removed" not in obj and not partial:
        errs.append("%s: missing lines_removed (integer, 0 if not applicable)" % where)


def main():
    root, repo, chunk = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]
    verify = "--verify" in sys.argv
    errs = []
    fpath = os.path.join(root, "findings", repo, chunk + ".json")
    try:
        with open(fpath, encoding="utf-8") as fh:
            data = json.load(fh)
    except Exception as e:  # noqa: BLE001
        print("ERROR: cannot parse %s: %s" % (fpath, e))
        sys.exit(1)
    findings = data.get("findings") if isinstance(data, dict) else None
    if not isinstance(findings, list):
        print("ERROR: %s has no 'findings' list" % fpath)
        sys.exit(1)
    if any(not isinstance(f, dict) for f in findings):
        print("ERROR: every entry of 'findings' must be an object")
        sys.exit(1)

    if not verify:
        if data.get("repo") != repo:
            errs.append("top-level repo must be %r" % repo)
        if data.get("chunk") != chunk:
            errs.append("top-level chunk must be %r" % chunk)
        if not isinstance(data.get("module_summary"), str) or len(data["module_summary"].strip()) < 20:
            errs.append("module_summary is required (2-6 sentences)")
        cov = data.get("coverage")
        if not isinstance(cov, list):
            errs.append("coverage must be a list")
            cov = []
        covered = {}
        for c in cov:
            if not isinstance(c, dict) or "path" not in c or c.get("status") not in (
                    "read_full", "read_partial", "skimmed", "not_read"):
                errs.append("coverage entry needs path and status in read_full|read_partial|skimmed|not_read: %r" % (c,))
                continue
            if c["status"] != "read_full" and not str(c.get("note", "")).strip():
                errs.append("coverage %s: status %s needs a note saying what was not read and why" % (c["path"], c["status"]))
            covered[c["path"]] = c
        plan_path = os.path.join(root, "plan", repo + ".json")
        if os.path.isfile(plan_path):
            with open(plan_path, encoding="utf-8") as fh:
                plan = json.load(fh)
            base = chunk[:-2] if chunk.endswith(".b") else chunk  # ".b" = second (defects) pass on the same chunk
            pc = next((c for c in plan["chunks"] if c["id"] == base), None)
            if pc:
                for f in pc["files"]:
                    if f["path"] not in covered:
                        errs.append("coverage is missing planned file %s" % f["path"])
        seen = set()
        for i, f in enumerate(findings):
            where = "finding #%s" % f.get("n", "?(index %d)" % i)
            if not isinstance(f.get("n"), int) or f["n"] in seen:
                errs.append("%s: 'n' must be a unique integer" % where)
            seen.add(f.get("n"))
            check_text(f, where, errs)
            check_enums(f, where, errs)
            check_locations(root, repo, f.get("locations"), where, errs)
            if not isinstance(f.get("evidence"), str) or len(f["evidence"].strip()) < 15:
                errs.append("%s: evidence is required (what you searched/read and what you found)" % where)
        for h in data.get("reusable_helpers", []) or []:
            if not isinstance(h, dict) or not h.get("name") or not h.get("path"):
                errs.append("reusable_helpers entries need name, path, line, what")
    else:
        vpath = os.path.join(root, "findings", repo, chunk + ".verify.json")
        try:
            with open(vpath, encoding="utf-8") as fh:
                v = json.load(fh)
        except Exception as e:  # noqa: BLE001
            print("ERROR: cannot parse %s: %s" % (vpath, e))
            sys.exit(1)
        verdicts = v.get("verdicts")
        if not isinstance(verdicts, list):
            print("ERROR: %s has no 'verdicts' list" % vpath)
            sys.exit(1)
        ns = {f.get("n") for f in findings}
        got = set()
        for d in verdicts:
            if not isinstance(d, dict):
                errs.append("every entry of 'verdicts' must be an object")
                continue
            where = "verdict for #%s" % d.get("n")
            if d.get("n") not in ns:
                errs.append("%s: no such finding number" % where)
                continue
            if d["n"] in got:
                errs.append("%s: duplicate verdict" % where)
            got.add(d["n"])
            if d.get("verdict") not in ("confirmed", "corrected", "refuted"):
                errs.append("%s: verdict must be confirmed|corrected|refuted" % where)
            if not isinstance(d.get("note"), str) or len(d["note"].strip()) < 15:
                errs.append("%s: note is required (what you re-checked and what you saw)" % where)
            elif len(d["note"]) > 1200:
                errs.append("%s: note is %d chars, limit 1200" % (where, len(d["note"])))
            else:
                for rx in SECRET:
                    if rx.search(d["note"]):
                        errs.append("%s: note contains a secret-like or hash-like string" % where)
                        break
            c = d.get("corrected")
            if d.get("verdict") == "corrected":
                if not isinstance(c, dict) or not c:
                    errs.append("%s: verdict 'corrected' needs a 'corrected' object with the fields to replace" % where)
                    continue
            if isinstance(c, dict):
                check_text(c, where + " corrected", errs, required=())
                check_enums(c, where + " corrected", errs, partial=True)
                if "locations" in c:
                    check_locations(root, repo, c["locations"], where + " corrected", errs)
        missing = sorted(n for n in ns - got if n is not None)
        if missing:
            errs.append("no verdict for finding number(s): %s" % missing)

    if errs:
        print("INVALID (%d problem%s):" % (len(errs), "" if len(errs) == 1 else "s"))
        for e in errs:
            print(" - " + e)
        sys.exit(1)
    print("OK: %s %s%s (%d finding%s)" % (repo, chunk, " verify" if verify else "", len(findings),
                                          "" if len(findings) == 1 else "s"))


if __name__ == "__main__":
    main()
