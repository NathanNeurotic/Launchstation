"""Render the per-repo audit report from findings + verification files.

Usage: python -I render.py <work_root> <Repo> <out_md> [--final]
Also writes findings/<Repo>/_final.json (the merged, ordered finding list with assigned IDs).
"""
import csv
import glob
import json
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402

CATNAME = {1: "Dead code", 2: "Duplication", 3: "Overengineering", 4: "Size and memory", 5: "Performance",
           6: "Bugs and security", 7: "Build/CI/repo clutter", 8: "Half-finished or redundant features"}
CONF_ORDER = {"High": 0, "Med": 1, "Low": 2}
EFF_ORDER = {"S": 0, "M": 1, "L": 2}
OVERRIDABLE = ("title", "category", "locations", "problem", "fix", "justification", "benefit", "benefit_rank",
               "lines_removed", "effort", "risk", "confidence", "basis")


def load_all(root, repo):
    """Returns (findings by key, coverage entries, stats, chunk ids seen)."""
    fdir = os.path.join(root, "findings", repo)
    out, coverage, seen_chunks = {}, [], set()
    stats = Counter()
    for path in sorted(glob.glob(os.path.join(fdir, "*.json"))):
        base = os.path.basename(path)
        if base.startswith("_") or base.endswith(".verify.json"):
            continue
        try:
            with open(path, encoding="utf-8") as fh:
                data = json.load(fh)
        except Exception:  # noqa: BLE001
            stats["unparseable_files"] += 1
            continue
        chunk = base[:-5]
        seen_chunks.add(chunk)
        for c in data.get("coverage", []) or []:
            if isinstance(c, dict) and "path" in c:
                coverage.append(dict(c, chunk=chunk))
        verdicts = None
        vpath = os.path.join(fdir, chunk + ".verify.json")
        if os.path.isfile(vpath):
            try:
                with open(vpath, encoding="utf-8") as fh:
                    verdicts = {d.get("n"): d for d in json.load(fh).get("verdicts", [])}
            except Exception:  # noqa: BLE001
                stats["unparseable_verify_files"] += 1
        for f in data.get("findings", []) or []:
            if not isinstance(f, dict) or "n" not in f:
                continue
            stats["raised"] += 1
            f = dict(f)
            f["key"] = "%s#%s" % (chunk, f["n"])
            f["verified"] = False
            if verdicts is not None and f["n"] in verdicts:
                d = verdicts[f["n"]]
                v = d.get("verdict")
                if v == "refuted":
                    stats["refuted"] += 1
                    continue
                if v == "corrected" and isinstance(d.get("corrected"), dict):
                    for k in OVERRIDABLE:
                        if k in d["corrected"]:
                            f[k] = d["corrected"][k]
                    stats["corrected"] += 1
                else:
                    stats["confirmed"] += 1
                f["verified"] = True
            else:
                stats["unverified"] += 1
            out[f["key"]] = f
    return out, coverage, stats, seen_chunks


def apply_merges(root, repo, findings, stats):
    mpath = os.path.join(root, "findings", repo, "_merge.json")
    if not os.path.isfile(mpath):
        return
    with open(mpath, encoding="utf-8") as fh:
        m = json.load(fh)
    for d in m.get("drop", []) or []:
        if d.get("key") in findings:
            del findings[d["key"]]
            stats["dropped_in_consolidation"] += 1
    for g in m.get("groups", []) or []:
        keep = g.get("keep")
        if keep not in findings:
            continue
        k = findings[keep]
        locs = list(k.get("locations", []))
        have = {(l.get("repo", repo), l.get("path"), l.get("start"), l.get("end")) for l in locs}
        for a in g.get("absorb", []) or []:
            if a in findings and a != keep:
                for l in findings[a].get("locations", []):
                    t = (l.get("repo", repo), l.get("path"), l.get("start"), l.get("end"))
                    if t not in have:
                        have.add(t)
                        locs.append(l)
                k["benefit_rank"] = max(k.get("benefit_rank", 1), findings[a].get("benefit_rank", 1))
                k["verified"] = k["verified"] and findings[a]["verified"]
                del findings[a]
                stats["merged_duplicates"] += 1
        k["locations"] = locs
        for fld, val in (g.get("merged") or {}).items():
            if fld in OVERRIDABLE:
                k[fld] = val


def fmt_loc(repo, l):
    r = l.get("repo", repo)
    if l.get("whole") or "start" not in l:
        return "`%s/%s`" % (r, l["path"])
    if l["start"] == l["end"]:
        return "`%s/%s:%d`" % (r, l["path"], l["start"])
    return "`%s/%s:%d-%d`" % (r, l["path"], l["start"], l["end"])


def one_line(s):
    return " ".join(str(s or "").split())


def render_finding(repo, fid, f):
    lines = ["#### %s: %s" % (fid, one_line(f.get("title"))), "",
             "- Category: %s (%s)" % (f.get("category"), CATNAME.get(f.get("category"), "?")),
             "- Location(s): " + "; ".join(fmt_loc(repo, l) for l in f.get("locations", [])),
             "- Problem: " + one_line(f.get("problem")),
             "- Fix: " + one_line(f.get("fix"))]
    if one_line(f.get("justification")):
        lines.append("- Justification: " + one_line(f.get("justification")))
    lines.append("- Benefit: " + one_line(f.get("benefit")))
    lines.append("- Effort: %s. Risk: %s. Confidence: %s (%s)" % (
        f.get("effort"), f.get("risk"), f.get("confidence"), f.get("basis")))
    return lines


def main():
    root, repo, out_md = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]
    final = "--final" in sys.argv
    verified_only = "--verified-only" in sys.argv
    with open(os.path.join(root, "plan", repo + ".json"), encoding="utf-8") as fh:
        plan = json.load(fh)
    findings, coverage, stats, seen_chunks = load_all(root, repo)
    apply_merges(root, repo, findings, stats)

    def sort_key(f):
        return (-int(f.get("benefit_rank", 1)), -int(f.get("lines_removed", 0) or 0),
                CONF_ORDER.get(f.get("confidence"), 3), EFF_ORDER.get(f.get("effort"), 3), one_line(f.get("title")))

    verified = sorted((f for f in findings.values() if f["verified"]), key=sort_key)
    unverified = sorted((f for f in findings.values() if not f["verified"]), key=sort_key)
    pending_count = len(unverified)
    if verified_only:
        unverified = []
    prefix = rules.PREFIX[repo]
    width = 3 if len(verified) + len(unverified) < 1000 else 4
    numbered = []
    for i, f in enumerate(verified + unverified, 1):
        f["id"] = "%s-%0*d" % (prefix, width, i)
        numbered.append(f)
    with open(os.path.join(root, "findings", repo, "_final.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"repo": repo, "commit": plan["commit"], "final": final, "findings": numbered}, fh, indent=1)

    units_by_chunk = defaultdict(list)
    upath = os.path.join(root, "plan", repo + ".units.json")
    if os.path.isfile(upath):
        with open(upath, encoding="utf-8") as fh:
            for u in json.load(fh):
                units_by_chunk[u["chunk"]].append(u["id"])
    for c in plan["chunks"]:
        units_by_chunk.setdefault(c["id"], [c["id"]])
    planned_chunks = [u for c in plan["chunks"] for u in units_by_chunk[c["id"]]]
    done_chunks = [u for u in planned_chunks if u in seen_chunks]
    md = ["# Claude audit: %s" % repo, ""]
    if final:
        md.append("Status: COMPLETE.")
    else:
        md.append("Status: IN PROGRESS. %d of %d reading passes are in; finding IDs are provisional and will be "
                  "renumbered until this line says COMPLETE." % (len(done_chunks), len(planned_chunks)))
    md += ["",
           "- Repository: NathanNeurotic/%s" % repo,
           "- Audited branch: %s" % rules.DEFAULT_BRANCH[repo],
           "- Audited commit: %s" % plan["commit"],
           "- Audit date: 2026-11-16",
           "- Mode: read-only review. This branch adds only the `Claude-audit/` folder; no source, build or workflow file is changed.",
           "",
           "All `path:line` references are pinned to the audited commit above. Each finding was raised by a reader "
           "that read the cited files line by line, then re-checked by a second, independent pass that re-read the "
           "cited lines and re-ran the reference searches. Findings that did not survive that check were dropped.",
           ""]
    md += ["## Totals", "",
           "- Findings in this report: %d%s" % (len(verified), (" verified, plus %d not yet verified" % len(unverified)) if unverified else (
               (". %d more raised findings are waiting for their second check and are not shown yet." % pending_count) if pending_count else "")),
           "- Raised by readers: %d. Dropped as wrong or unsupported on re-check: %d. Corrected on re-check: %d. Merged as duplicates: %d." % (
               stats["raised"], stats["refuted"] + stats["dropped_in_consolidation"], stats["corrected"], stats["merged_duplicates"]),
           ""]
    bycat = Counter(f.get("category") for f in verified)
    byrank = Counter(f.get("benefit_rank") for f in verified)
    md += ["| Category | Count |", "|---|---:|"]
    md += ["| %d %s | %d |" % (c, CATNAME[c], bycat.get(c, 0)) for c in sorted(CATNAME)]
    md += ["", "| Benefit rank | Meaning | Count |", "|---|---|---:|",
           "| 5 | major: real crash/data-loss/security bug, or >= 1000 lines / >= 50 KB / >= 100 KB RAM / multi-second freeze | %d |" % byrank.get(5, 0),
           "| 4 | large: >= 300 lines / >= 10 KB / >= 32 KB RAM, clear reliability or responsiveness gain | %d |" % byrank.get(4, 0),
           "| 3 | moderate: >= 80 lines / >= 2 KB, minor bug, redundant CI job or card write | %d |" % byrank.get(3, 0),
           "| 2 | small: >= 15 lines, a few hundred bytes | %d |" % byrank.get(2, 0),
           "| 1 | trivial: a few lines, cosmetic | %d |" % byrank.get(1, 0), ""]

    md += ["## Findings (sorted by benefit)", ""]
    if not verified:
        md += ["None verified yet.", ""]
    cur_rank = None
    for f in verified:
        if f.get("benefit_rank") != cur_rank:
            cur_rank = f.get("benefit_rank")
            md += ["### Benefit rank %s" % cur_rank, ""]
        md += render_finding(repo, f["id"], f) + [""]
    if unverified:
        md += ["### Not yet verified (raised by a reader, second check still pending)", ""]
        for f in unverified:
            md += render_finding(repo, f["id"], f) + [""]

    # ---- stale branches and tags (list only)
    refs_path = os.path.join(root, "evidence", repo, "git-refs.tsv")
    if os.path.isfile(refs_path):
        open_heads = set()
        oh = os.path.join(root, "evidence", repo, "open-pr-heads.txt")
        if os.path.isfile(oh):
            with open(oh, encoding="utf-8") as fh:
                open_heads = {l.strip() for l in fh if l.strip()}
        with open(refs_path, encoding="utf-8") as fh:
            refs = list(csv.DictReader(fh, delimiter="\t", quoting=csv.QUOTE_NONE))
        branches = [r for r in refs if r["type"] == "branch" and r["state"] != "default"]
        tags = [r for r in refs if r["type"] == "tag"]
        merged = [r for r in branches if r["state"] == "merged"]
        old = [r for r in branches if r["state"] != "merged" and r["last_commit"] < "2026-10-17"]
        md += ["## Stale branches and tags (list only)", "",
               "Listed for information; nothing here is a recommendation to delete without checking. Snapshot of the "
               "remote at audit time: %d branches besides the default, %d tags. \"Open PR\" marks a branch that is the "
               "head of an open pull request." % (len(branches), len(tags)), ""]
        md += ["### Branches fully merged into %s (%d)" % (rules.DEFAULT_BRANCH[repo], len(merged)), ""]
        if merged:
            md += ["| Branch | Last commit | Note |", "|---|---|---|"]
            md += ["| `%s` | %s | %s |" % (r["name"], r["last_commit"], "open PR" if r["name"] in open_heads else "")
                   for r in merged]
        else:
            md.append("None.")
        md += ["", "### Unmerged branches with no commit in the 30 days before the audit (%d)" % len(old), ""]
        if old:
            md += ["| Branch | Last commit | Ahead | Behind | Note |", "|---|---|---:|---:|---|"]
            md += ["| `%s` | %s | %s | %s | %s |" % (r["name"], r["last_commit"], r["ahead_of_default"],
                                                    r["behind_default"], "open PR" if r["name"] in open_heads else "")
                   for r in old]
        else:
            md.append("None.")
        md += ["", "### Tags (%d)" % len(tags), ""]
        if len(tags) <= 40:
            if tags:
                md += ["| Tag | Date |", "|---|---|"]
                md += ["| `%s` | %s |" % (r["name"], r["last_commit"]) for r in tags]
            else:
                md.append("None.")
        else:
            groups = defaultdict(list)
            for r in tags:
                name = r["name"]
                stem = name.rstrip("0123456789.-_")
                for sep in ("-", "_", "."):
                    if sep in stem:
                        stem = stem.split(sep)[0]
                        break
                groups[stem or name].append(r)
            md += ["| Tag family | Count | First | Last | Examples |", "|---|---:|---|---|---|"]
            for stem, rs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
                dates = sorted(r["last_commit"] for r in rs)
                md.append("| `%s*` | %d | %s | %s | %s |" % (
                    stem, len(rs), dates[0], dates[-1], ", ".join("`%s`" % r["name"] for r in rs[:3])))
        md.append("")

    # ---- files not read
    md += ["## Files not read line by line (and why)", ""]
    cov_by_path = defaultdict(list)
    for c in coverage:
        cov_by_path[c["path"]].append(c)
    not_full, pending = [], Counter()
    skim_lines = []
    for ch in plan["chunks"]:
        if ch["tier"] == "skim":
            status = "structural review only" if ch["id"] in seen_chunks else "not reviewed yet"
            skim_lines.append("- %s: %d files, %d lines. %s (config, build usage, size and whether it is needed), not a line-by-line read." % (
                ch["label"], len(ch["files"]), ch["lines"], status.capitalize()))
            continue
        ids = units_by_chunk[ch["id"]]
        missing = [i for i in ids if i not in seen_chunks]
        for f in ch["files"]:
            if missing:
                pending[ch["id"]] += 1
                continue
            entries = [c for c in cov_by_path.get(f["path"], []) if c["chunk"] in ids]
            if not entries:
                not_full.append((f["path"], "no coverage record returned by the reader"))
            else:
                for c in entries:
                    if c.get("status") != "read_full":
                        which = " (second pass)" if c["chunk"].endswith(".b") else ""
                        not_full.append((f["path"], "%s%s: %s" % (c.get("status"), which, one_line(c.get("note")))))
    if skim_lines:
        md += ["Vendored or generated content, reviewed structurally:", ""] + skim_lines + [""]
    by_reason = defaultdict(lambda: [0, 0, Counter()])
    for l in plan["listed"]:
        e = by_reason[l["reason"]]
        e[0] += 1
        e[1] += l["bytes"]
        e[2][l["path"].split("/")[0] if "/" in l["path"] else "(root)"] += 1
    if by_reason:
        md += ["Not read as text:", ""]
        for reason, (n, b, dirs) in sorted(by_reason.items(), key=lambda kv: -kv[1][0]):
            top = ", ".join("`%s` (%d)" % (d, c) for d, c in dirs.most_common(8))
            md.append("- %d files, %.1f MB: %s. Mostly under %s." % (n, b / 1048576.0, reason, top))
        md.append("")
    if not_full:
        md += ["Assigned for a full read but not fully read:", ""]
        md += ["- `%s`: %s" % x for x in sorted(set(not_full))]
        md.append("")
    if pending:
        md += ["Not read yet (reading still in progress): %d files in %d chunks." % (sum(pending.values()), len(pending)), ""]
    if not (skim_lines or by_reason or not_full or pending):
        md += ["Every tracked text file was read in full.", ""]

    os.makedirs(os.path.dirname(os.path.abspath(out_md)), exist_ok=True)
    with open(out_md, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(md).rstrip("\n") + "\n")
    print("%s: %d verified, %d unverified, raised %d, refuted %d, corrected %d, merged %d; chunks %d/%d -> %s" % (
        repo, len(verified), len(unverified), stats["raised"], stats["refuted"], stats["corrected"],
        stats["merged_duplicates"], len(done_chunks), len(planned_chunks), out_md))


if __name__ == "__main__":
    main()
