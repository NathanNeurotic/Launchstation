"""Per-chunk lead sheets built from the mechanical evidence (hints only, unverified).

Usage: python -I leads.py <work_root>
Writes leads/<Repo>/<chunk-id>.md for every chunk that has at least one lead.
"""
import csv
import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402

CAP = {"symbols": 120, "blocks": 60, "pairs": 40, "exact": 40, "macros": 60, "if0": 60, "commented": 60,
       "markers": 60, "files": 60}


def rows(path):
    if not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8", errors="replace", newline="") as fh:
        return list(csv.DictReader(fh, delimiter="\t", quoting=csv.QUOTE_NONE))


def to_int(v, default=0):
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


def main():
    root = os.path.abspath(sys.argv[1])
    cross = {k: rows(os.path.join(root, "evidence", "_cross", "dupes-%s.tsv" % k))
             for k in ("blocks", "file-pairs", "exact-files")}
    for repo in rules.REPOS:
        with open(os.path.join(root, "plan", repo + ".json"), encoding="utf-8") as fh:
            plan = json.load(fh)
        ev = os.path.join(root, "evidence", repo)
        where = defaultdict(list)  # path -> [(chunk, start, end)]
        for ch in plan["chunks"]:
            if ch["tier"] == "skim":
                continue
            for f in ch["files"]:
                where[f["path"]].append((ch["id"], f.get("start", 1) if f.get("part") else 1,
                                         f.get("end", 10 ** 9) if f.get("part") else 10 ** 9))

        def chunks_for(path, line=None, end=None):
            out = []
            for cid, s, e in where.get(path, []):
                if line is None or (line <= e and (end if end is not None else line) >= s):
                    out.append(cid)
            return out

        lead = defaultdict(lambda: defaultdict(list))

        for r in rows(os.path.join(ev, "symbols-unreferenced.tsv")):
            for cid in chunks_for(r["path"], to_int(r["line"], 1)):
                extra = (" | also mentioned: " + r["other_mentions"]) if r.get("other_mentions") else ""
                lead[cid]["symbols"].append("%s:%s  %s `%s`  (%s)%s" % (
                    r["path"], r["line"], r["kind"], r["name"], r["class"], extra))
        for r in rows(os.path.join(ev, "files-unreferenced.tsv")):
            for cid in chunks_for(r["path"]):
                lead[cid]["files"].append("%s  (%s lines; mentioned in %s docs)" % (
                    r["path"], r["lines"], r.get("doc_mentions", "?")))
        for r in rows(os.path.join(ev, "cond-if0.tsv")):
            for cid in chunks_for(r["path"], to_int(r["start"], 1), to_int(r["end"], 1)):
                lead[cid]["if0"].append("%s:%s-%s  %s (%s lines)" % (r["path"], r["start"], r["end"], r["kind"], r["lines"]))
        for r in rows(os.path.join(ev, "cond-commented-code.tsv")):
            for cid in chunks_for(r["path"], to_int(r["start"], 1), to_int(r["end"], 1)):
                lead[cid]["commented"].append("%s:%s-%s  %s code-like lines" % (
                    r["path"], r["start"], r["end"], r["code_like_lines"]))
        for r in rows(os.path.join(ev, "cond-markers.tsv")):
            for cid in chunks_for(r["path"], to_int(r["line"], 1)):
                lead[cid]["markers"].append("%s:%s  %s" % (r["path"], r["line"], " ".join(r["text"].split())[:140]))
        for r in rows(os.path.join(ev, "cond-macros-undefined.tsv")):
            hit = set()
            for site in (r.get("sample_sites") or "").split(";"):
                site = site.strip()
                if ":" not in site:
                    continue
                p, _, ln = site.rpartition(":")
                for cid in chunks_for(p, to_int(ln, 1)):
                    hit.add((cid, site))
            per = defaultdict(list)
            for cid, site in hit:
                per[cid].append(site)
            for cid, sites in per.items():
                lead[cid]["macros"].append("`%s`  tested at %s site(s) in the repo, mentioned in %s build file(s)%s; here: %s" % (
                    r["macro"], r["test_sites"], r["build_file_mentions"],
                    (" [" + r["class"] + "]") if r.get("class") else "", ", ".join(sorted(sites))))

        def dup_rows(local, crossrows):
            return list(local) + [r for r in crossrows if r.get("repoA") == repo or r.get("repoB") == repo]

        for r in dup_rows(rows(os.path.join(ev, "dupes-blocks.tsv")), cross["blocks"]):
            for side, other in (("A", "B"), ("B", "A")):
                if r["repo" + side] != repo:
                    continue
                for cid in chunks_for(r["path" + side], to_int(r["start" + side], 1), to_int(r["end" + side], 1)):
                    lead[cid]["blocks"].append("%s:%s-%s  ==  %s/%s:%s-%s  (%s normalised lines)" % (
                        r["path" + side], r["start" + side], r["end" + side], r["repo" + other], r["path" + other],
                        r["start" + other], r["end" + other], r["norm_lines"]))
        for r in dup_rows(rows(os.path.join(ev, "dupes-file-pairs.tsv")), cross["file-pairs"]):
            if r["repoA"] == r["repoB"] and r["pathA"] == r["pathB"]:
                if r["repoA"] == repo:
                    for cid in chunks_for(r["pathA"]):
                        lead[cid]["pairs"].append("%s repeats itself: %s cloned normalised lines inside the file (%s)" % (
                            r["pathA"], r["cloned_norm_lines"], r["pctA"]))
                continue
            for side, other in (("A", "B"), ("B", "A")):
                if r["repo" + side] != repo:
                    continue
                for cid in chunks_for(r["path" + side]):
                    lead[cid]["pairs"].append("%s is %s the same as %s/%s (%s cloned normalised lines)" % (
                        r["path" + side], r["pct" + side], r["repo" + other], r["path" + other], r["cloned_norm_lines"]))
        for src in (rows(os.path.join(ev, "dupes-exact-files.tsv")), cross["exact-files"]):
            groups = defaultdict(list)
            for r in src:
                groups[r["group"]].append(r)
            for g in groups.values():
                for r in g:
                    if r["repo"] != repo:
                        continue
                    others = ["%s/%s" % (o["repo"], o["path"]) for o in g if o is not r]
                    for cid in chunks_for(r["path"]):
                        lead[cid]["exact"].append("%s is byte-identical to: %s" % (r["path"], "; ".join(others[:8]) + (
                            " (+%d more)" % (len(others) - 8) if len(others) > 8 else "")))

        out = os.path.join(root, "leads", repo)
        os.makedirs(out, exist_ok=True)
        titles = [("files", "Files that no other file mentions by name (files-unreferenced.tsv)"),
                  ("symbols", "Symbols with no reference outside their definition (symbols-unreferenced.tsv). "
                              "Class no-other-mention = the name appears nowhere else in the repo; "
                              "only-prototype-or-export-mentions = it appears only in a prototype or export list"),
                  ("if0", "Disabled blocks (cond-if0.tsv)"),
                  ("commented", "Comment blocks that look like code (cond-commented-code.tsv)"),
                  ("macros", "Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). "
                             "Compiler, SDK and toolchain macros also land here: check before using"),
                  ("exact", "Byte-identical files (dupes-exact-files.tsv)"),
                  ("pairs", "Largely identical files (dupes-file-pairs.tsv)"),
                  ("blocks", "Duplicate blocks (dupes-blocks.tsv)"),
                  ("markers", "TODO/FIXME/HACK style markers (cond-markers.tsv)")]
        n = 0
        for ch in plan["chunks"]:
            d = lead.get(ch["id"])
            if not d:
                continue
            md = []
            for key, title in titles:
                items = sorted(set(d.get(key, [])))
                if not items:
                    continue
                md.append("## " + title)
                md += ["- " + x for x in items[:CAP[key]]]
                if len(items) > CAP[key]:
                    md.append("- ... %d more: grep your file paths in evidence/%s/" % (len(items) - CAP[key], repo))
                md.append("")
            with open(os.path.join(out, ch["id"] + ".md"), "w", encoding="utf-8", newline="\n") as fh:
                fh.write("\n".join(md))
            n += 1
        print("%s: lead sheets for %d of %d chunks" % (repo, n, len(plan["chunks"])))


if __name__ == "__main__":
    main()
