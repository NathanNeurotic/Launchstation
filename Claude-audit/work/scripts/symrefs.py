"""Unreferenced-symbol and unreferenced-file candidates for one repo (evidence only, candidates not verdicts).

Usage: python -I symrefs.py <work_root> <Repo>
Outputs under evidence/<Repo>/ :
  symbols-unreferenced.tsv  definitions (ctags) whose name appears nowhere else in the repo's non-doc text,
                            or only in what looks like a prototype / export entry
  files-unreferenced.tsv    source files whose name (or stem / .o / .irx) is not mentioned by any other non-doc file
"""
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402

IDENT = re.compile(r"[A-Za-z_]\w*")
PATHTOK = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_\-.]*")
SKIP_NAMES = {"main", "_start", "_init", "_fini", "_exit", "setup", "teardown", "__init__", "__main__",
              "module_start", "module_stop", "_retonly"}
DEF_TIERS = ("core", "module", "codec", "support", "buildci")


def main():
    root, repo = os.path.abspath(sys.argv[1]), sys.argv[2]
    src = os.path.join(root, "src", repo)
    out = os.path.join(root, "evidence", repo)
    os.makedirs(out, exist_ok=True)
    files = []
    with open(os.path.join(root, "inventory", repo + ".tsv"), encoding="utf-8") as fh:
        next(fh)
        for line in fh:
            p, b, l, k = line.rstrip("\n").split("\t")
            tier, _ = rules.classify(repo, p, k)
            if k == "binary":
                continue
            files.append({"path": p, "kind": k, "tier": tier, "lines": int(l)})

    # ---- ctags over first-party definition files
    lst = os.path.join(root, "tmp", repo, "ctags-files.txt")
    os.makedirs(os.path.dirname(lst), exist_ok=True)
    exts = (".c", ".h", ".cpp", ".hpp", ".cc", ".py", ".lua")
    with open(lst, "w", encoding="utf-8", newline="\n") as fh:
        for f in files:
            if f["tier"] in DEF_TIERS and f["path"].lower().endswith(exts):
                fh.write(f["path"] + "\n")
    proc = subprocess.run(
        ["ctags", "--output-format=json", "--fields=+nKSf", "--extras=-F", "--languages=C,C++,Python,Lua",
         "--kinds-C=fvd", "--kinds-C++=fvd", "--kinds-Python=fcm", "--kinds-Lua=f", "-L", lst, "-f", "-"],
        cwd=src, capture_output=True)
    tags = []
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        if not line.startswith("{"):
            continue
        try:
            t = json.loads(line)
        except ValueError:
            continue
        if t.get("_type") == "tag" and "line" in t:
            tags.append(t)
    defs = defaultdict(list)
    for t in tags:
        name = t["name"]
        tpath = t["path"].replace("\\", "/")
        tbase = os.path.basename(tpath)
        if tpath.endswith(".py") and (tbase.startswith("test_") or tbase == "conftest.py" or "/tests/" in "/" + tpath):
            # unittest/pytest discover these by name; not dead-code candidates
            if t.get("kind") in ("class", "member") or name.startswith("test_"):
                continue
        if len(name) < 3 or name in SKIP_NAMES or name.startswith("__") and name.endswith("__"):
            continue
        if "." in name or ":" in name:
            name = re.split(r"[.:]", name)[-1]
            if len(name) < 3:
                continue
        defs[name].append(t)

    # ---- pass 1: token totals over non-doc text; path-token index
    counts = Counter()
    pathtok = defaultdict(set)      # token -> set of file idx (non-doc), capped
    pathtok_doc = Counter()
    texts = {}
    for i, f in enumerate(files):
        if f["tier"] == "list":
            continue
        try:
            with open(os.path.join(src, f["path"]), "rb") as fh:
                text = fh.read().decode("utf-8", "replace")
        except OSError:
            continue
        is_doc = f["tier"] == "docs"
        if not is_doc:
            texts[i] = text
            counts.update(IDENT.findall(text))
        seen = set()
        for tok in PATHTOK.findall(text):
            if len(tok) < 3 or tok in seen:
                continue
            seen.add(tok)
        for tok in seen:
            if is_doc:
                pathtok_doc[tok] += 1
            else:
                s = pathtok[tok]
                if len(s) < 4:
                    s.add(i)

    # ---- pass 2: sites for candidate names
    cand = {n for n, ts in defs.items() if counts.get(n, 0) <= len(ts) + 2}
    sites = defaultdict(list)
    for i, text in texts.items():
        path = files[i]["path"]
        for n, line in enumerate(text.split("\n"), 1):
            for tok in IDENT.findall(line):
                if tok in cand and len(sites[tok]) < 12:
                    sites[tok].append((path, n, line.strip()[:110].replace("\t", " ")))
    rows = []
    for name in sorted(cand):
        ts = defs[name]
        defsites = {(t["path"].replace("\\", "/"), t["line"]) for t in ts}
        others = [s for s in sites.get(name, []) if (s[0], s[1]) not in defsites]
        proto = re.compile(r"^(extern\s+|static\s+|inline\s+)*[\w\s\*]+\b" + re.escape(name) + r"\s*\([^;{]*\)\s*;\s*$")
        if others:
            ok = all(proto.match(s[2]) or "DECLARE_EXPORT" in s[2] or "DECLARE_IMPORT" in s[2] for s in others)
            if not ok:
                continue
            cls = "only-prototype-or-export-mentions"
        else:
            cls = "no-other-mention"
        t = ts[0]
        tier = next((f["tier"] for f in files if f["path"] == t["path"].replace("\\", "/")), "")
        rows.append((cls, t.get("language", ""), t.get("kind", ""), name, t["path"].replace("\\", "/"), t["line"],
                     "file-scope" if t.get("file") else "", len(ts), tier,
                     "; ".join("%s:%d %s" % s for s in others[:4])))
    rows.sort(key=lambda r: (r[0], r[4], r[5]))
    with open(os.path.join(out, "symbols-unreferenced.tsv"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("class\tlang\tkind\tname\tpath\tline\tscope\tdefinitions\ttier\tother_mentions\n")
        for r in rows:
            fh.write("\t".join(str(x) for x in r) + "\n")

    # ---- unreferenced files
    frows = []
    for i, f in enumerate(files):
        if f["tier"] in ("list", "docs", "skim") or f["kind"] not in ("source",):
            continue
        base = os.path.basename(f["path"])
        stem, ext = os.path.splitext(base)
        variants = {base, stem, stem + ".o", stem + ".irx", stem + ".elf", stem + ".ELF", stem + ".h"}
        if ext.lower() == ".py" and base.startswith("test_"):
            continue  # discovered by the test runner
        ref = False
        for v in variants:
            if any(j != i for j in pathtok.get(v, ())):
                ref = True
                break
        if not ref:
            frows.append((f["path"], f["lines"], f["tier"], sum(pathtok_doc.get(v, 0) for v in (base, stem))))
    with open(os.path.join(out, "files-unreferenced.tsv"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("path\tlines\ttier\tdoc_mentions\n")
        for r in sorted(frows):
            fh.write("\t".join(str(x) for x in r) + "\n")
    print("%s: tags=%d names=%d symbol candidates=%d (no-other-mention %d) unreferenced-file candidates=%d" % (
        repo, len(tags), len(defs), len(rows), sum(1 for r in rows if r[0] == "no-other-mention"), len(frows)))


if __name__ == "__main__":
    main()
