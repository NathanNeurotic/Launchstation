"""Duplicate detection within and across the audited repos (evidence only).

Usage: python -I dupes.py <work_root>
Outputs under evidence/<Repo>/ and evidence/_cross/ :
  dupes-exact-files.tsv   byte-identical files (any type)
  dupes-file-pairs.tsv    file pairs ranked by cloned normalized lines
  dupes-blocks.tsv        cloned blocks (>= MIN_RUN normalized lines)
  dupes-boilerplate.tsv   windows repeated more than MAX_OCC times
"""
import hashlib
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402

W = 8
MIN_RUN = 12
MAX_OCC = 12
MIN_WINDOW_CHARS = 120

C_LIKE = {".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".hh", ".s", ".S", ".inc", ".go", ".js", ".ts", ".java", ".cs",
          ".vcl", ".vsm", ".ld"}
HASH_LIKE = {".py", ".sh", ".yml", ".yaml", ".mk", ".toml", ".cfg", ".ini", ".ps1", ".cmake", ".txt"}
LUA = {".lua"}
LOW_INFO = {"else", "else{", "}else{", "break;", "return;", "#endif", "end", "do", "then", "{", "}", "};", "});", ");",
            "try:", "pass", "continue;", "continue", "default:", "returnnil", "returntrue", "returnfalse"}
DATA_RX = re.compile(r"^[0-9a-fA-FxX,{}\-.+uUlL()\[\];=]*$")
WS = re.compile(r"\s+")


def norm(line, ext):
    s = line.strip()
    if not s:
        return None
    if ext in C_LIKE:
        if s.startswith("//") or s.startswith("/*") or s.startswith("*"):
            return None
    elif ext in LUA:
        if s.startswith("--"):
            return None
    elif ext in HASH_LIKE or ext == "":
        if s.startswith("#"):
            return None
    s = WS.sub("", s)
    if len(s) <= 3 or s in LOW_INFO:
        return None
    if len(s) >= 8 and DATA_RX.match(s):
        return 0  # data line: breaks windows
    return s


def main():
    root = sys.argv[1]
    files = []  # dict(repo, path, tier, kind, bytes, lines)
    for repo in rules.REPOS:
        with open(os.path.join(root, "inventory", repo + ".tsv"), encoding="utf-8") as fh:
            next(fh)
            for line in fh:
                p, b, l, k = line.rstrip("\n").split("\t")
                tier, _ = rules.classify(repo, p, k)
                files.append({"repo": repo, "path": p, "tier": tier, "kind": k, "bytes": int(b), "lines": int(l)})

    # ---------- exact duplicate files
    by_hash = defaultdict(list)
    for i, f in enumerate(files):
        if f["bytes"] < 32:
            continue
        try:
            with open(os.path.join(root, "src", f["repo"], f["path"]), "rb") as fh:
                data = fh.read()
        except OSError:
            continue
        by_hash[hashlib.sha1(data).hexdigest()].append(i)
    groups = [g for g in by_hash.values() if len(g) > 1]
    groups.sort(key=lambda g: -files[g[0]]["bytes"] * (len(g) - 1))

    def write(path, header, rows):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\t".join(header) + "\n")
            for r in rows:
                fh.write("\t".join(str(x) for x in r) + "\n")

    cross_rows, per_repo_rows = [], defaultdict(list)
    for gi, g in enumerate(groups, 1):
        repos = {files[i]["repo"] for i in g}
        for i in g:
            f = files[i]
            row = (gi, f["bytes"], len(g), f["repo"], f["path"], f["tier"], f["kind"])
            if len(repos) > 1:
                cross_rows.append(row)
            if sum(1 for j in g if files[j]["repo"] == f["repo"]) > 1:
                per_repo_rows[f["repo"]].append(row)
    hdr = ("group", "bytes", "copies_in_group", "repo", "path", "tier", "kind")
    write(os.path.join(root, "evidence", "_cross", "dupes-exact-files.tsv"), hdr, cross_rows)
    for repo in rules.REPOS:
        write(os.path.join(root, "evidence", repo, "dupes-exact-files.tsv"), hdr, per_repo_rows[repo])

    # ---------- block clones
    seqs = {}  # file index -> (norm list, lineno list)
    index = defaultdict(list)
    for i, f in enumerate(files):
        if f["kind"] not in ("source", "build", "othertext") or f["tier"] in ("list", "docs"):
            continue
        ext = os.path.splitext(f["path"])[1]
        if ext.lower() in (".json", ".lst", ".tab", ".xml", ".tsv", ".csv", ".sum", ".lng", ".svg"):
            continue
        try:
            with open(os.path.join(root, "src", f["repo"], f["path"]), "rb") as fh:
                text = fh.read().decode("utf-8", "replace")
        except OSError:
            continue
        ns, ls = [], []
        for n, line in enumerate(text.split("\n"), 1):
            v = norm(line, ext if ext in (".S",) else ext.lower())
            if v is None:
                continue
            ns.append(v)
            ls.append(n)
        seqs[i] = (ns, ls)
        f["norm_lines"] = sum(1 for v in ns if v != 0)
        for p in range(len(ns) - W + 1):
            win = ns[p:p + W]
            if 0 in win:
                continue
            if sum(len(x) for x in win) < MIN_WINDOW_CHARS:
                continue
            index[hash(tuple(win))].append((i, p))

    diag = defaultdict(list)
    boiler = []
    for h, occ in index.items():
        if len(occ) < 2:
            continue
        if len(occ) > MAX_OCC:
            boiler.append(occ)
            continue
        for a in range(len(occ)):
            fa, pa = occ[a]
            for b in range(a + 1, len(occ)):
                fb, pb = occ[b]
                if fa == fb:
                    if abs(pa - pb) < W:
                        continue
                    lo, hi = (pa, pb) if pa < pb else (pb, pa)
                    diag[(fa, fb, hi - lo)].append(lo)
                elif fa < fb:
                    diag[(fa, fb, pb - pa)].append(pa)
                else:
                    diag[(fb, fa, pa - pb)].append(pb)

    runs = []  # (fa, startA, endA, fb, startB, endB, nlines)
    pair_total = defaultdict(int)
    for (fa, fb, d), plist in diag.items():
        plist.sort()
        start = prev = plist[0]
        for p in plist[1:] + [None]:
            if p is not None and p == prev + 1:
                prev = p
                continue
            n = prev - start + W
            if n >= MIN_RUN:
                la, lb = seqs[fa][1], seqs[fb][1]
                pb0 = start + d
                runs.append((fa, la[start], la[prev + W - 1], fb, lb[pb0], lb[pb0 + (prev - start) + W - 1], n))
                pair_total[(fa, fb)] += n
            if p is not None:
                start = prev = p

    def fmt_run(r):
        fa, sa, ea, fb, sb, eb, n = r
        A, B = files[fa], files[fb]
        return (n, A["repo"], A["path"], sa, ea, A["tier"], B["repo"], B["path"], sb, eb, B["tier"])

    runs.sort(key=lambda r: -r[6])
    hdr = ("norm_lines", "repoA", "pathA", "startA", "endA", "tierA", "repoB", "pathB", "startB", "endB", "tierB")
    write(os.path.join(root, "evidence", "_cross", "dupes-blocks.tsv"), hdr,
          [fmt_run(r) for r in runs if files[r[0]]["repo"] != files[r[3]]["repo"]])
    for repo in rules.REPOS:
        write(os.path.join(root, "evidence", repo, "dupes-blocks.tsv"), hdr,
              [fmt_run(r) for r in runs if files[r[0]]["repo"] == repo and files[r[3]]["repo"] == repo])

    pairs = []
    for (fa, fb), n in pair_total.items():
        A, B = files[fa], files[fb]
        na, nb = max(A.get("norm_lines", 1), 1), max(B.get("norm_lines", 1), 1)
        pairs.append((n, A["repo"], A["path"], na, "%d%%" % min(100, 100 * n // na), A["tier"],
                      B["repo"], B["path"], nb, "%d%%" % min(100, 100 * n // nb), B["tier"]))
    pairs.sort(key=lambda r: -r[0])
    hdr = ("cloned_norm_lines", "repoA", "pathA", "normLinesA", "pctA", "tierA",
           "repoB", "pathB", "normLinesB", "pctB", "tierB")
    write(os.path.join(root, "evidence", "_cross", "dupes-file-pairs.tsv"), hdr,
          [p for p in pairs if p[1] != p[6]])
    for repo in rules.REPOS:
        write(os.path.join(root, "evidence", repo, "dupes-file-pairs.tsv"), hdr,
              [p for p in pairs if p[1] == repo and p[6] == repo])

    # ---------- boilerplate (very frequent windows), merged into runs per first file
    seen = set()
    brow = []
    boiler.sort(key=lambda occ: -len(occ))
    for occ in boiler:
        fa, pa = occ[0]
        if (fa, pa // W) in seen:
            continue
        seen.add((fa, pa // W))
        A = files[fa]
        locs = "; ".join("%s:%s:%d" % (files[i]["repo"], files[i]["path"], seqs[i][1][p]) for i, p in occ[:8])
        nfiles = len({i for i, _ in occ})
        repos = sorted({files[i]["repo"] for i, _ in occ})
        brow.append((len(occ), nfiles, ",".join(repos), A["repo"], A["path"], seqs[fa][1][pa], locs))
    hdr = ("occurrences", "distinct_files", "repos", "first_repo", "first_path", "first_line", "sample_locations")
    write(os.path.join(root, "evidence", "_cross", "dupes-boilerplate.tsv"), hdr, brow[:400])

    print("files=%d exact-dup groups=%d clone runs=%d (cross-repo %d) pairs=%d boilerplate windows=%d" % (
        len(files), len(groups), len(runs),
        sum(1 for r in runs if files[r[0]]["repo"] != files[r[3]]["repo"]), len(pairs), len(boiler)))


if __name__ == "__main__":
    main()
