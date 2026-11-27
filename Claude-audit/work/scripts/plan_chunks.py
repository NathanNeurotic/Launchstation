"""Build the reading plan (chunks) for one repo from its inventory.

Usage: python -I plan_chunks.py <work_root> <Repo> [-v]
Writes plan/<Repo>.json and prints a summary. Every tracked file ends up in
exactly one chunk or in the 'listed' set with a reason.
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402


def load_inventory(root, repo):
    rows = []
    with open(os.path.join(root, "inventory", repo + ".tsv"), encoding="utf-8") as fh:
        next(fh)
        for line in fh:
            p, b, l, k = line.rstrip("\n").split("\t")
            rows.append({"path": p, "bytes": int(b), "lines": int(l), "kind": k})
    return rows


def pack(items, limit):
    """items: ordered list of dicts with path/lines/group. Returns list of chunks (lists of file entries)."""
    chunks, cur, cur_lines, cur_group = [], [], 0, None
    for it in items:
        n = it["lines"]
        if n > limit * 1.6:
            # very large single file: own chunk(s), split by line ranges
            if cur:
                chunks.append(cur)
                cur, cur_lines = [], 0
            parts = -(-n // limit)
            size = -(-n // parts)
            start = 1
            while start <= n:
                end = min(n, start + size - 1)
                chunks.append([dict(it, start=start, end=end, lines=end - start + 1, part=True)])
                start = end + 1
            cur_group = None
            continue
        new_group = it["group"] != cur_group
        if cur and (cur_lines + n > limit or (new_group and cur_lines >= limit * 0.6)):
            chunks.append(cur)
            cur, cur_lines = [], 0
        cur.append(dict(it, start=1, end=n))
        cur_lines += n
        cur_group = it["group"]
    if cur:
        chunks.append(cur)
    # merge tiny chunks into a neighbour of the same tier when it fits
    def is_part(ch):
        return len(ch) == 1 and ch[0].get("part")

    def size(ch):
        return sum(f["lines"] for f in ch)

    changed = True
    while changed:
        changed = False
        for i, ch in enumerate(chunks):
            if is_part(ch) or size(ch) >= limit * 0.25:
                continue
            cands = [j for j in (i - 1, i + 1) if 0 <= j < len(chunks) and not is_part(chunks[j])
                     and size(chunks[j]) + size(ch) <= limit * 1.15]
            if not cands:
                # look further for any non-part chunk with room
                cands = [j for j in range(len(chunks)) if j != i and not is_part(chunks[j])
                         and size(chunks[j]) + size(ch) <= limit * 1.15]
                cands.sort(key=lambda j: abs(j - i))
            if cands:
                j = cands[0]
                if j < i:
                    chunks[j] = chunks[j] + ch
                else:
                    chunks[j] = ch + chunks[j]
                del chunks[i]
                changed = True
                break
    return chunks


def main():
    root, repo = sys.argv[1], sys.argv[2]
    rows = load_inventory(root, repo)
    commit = subprocess.run(["git", "-C", os.path.join(root, "src", repo), "rev-parse", "HEAD"],
                            capture_output=True, text=True, check=True).stdout.strip()
    by_tier, listed, skim = {}, [], {}
    for r in rows:
        tier, label = rules.classify(repo, r["path"], r["kind"])
        r["tier"] = tier
        if tier == "list":
            listed.append({"path": r["path"], "bytes": r["bytes"], "lines": r["lines"], "reason": label})
        elif tier == "skim":
            skim.setdefault(label, []).append(r)
        else:
            if r["lines"] == 0:
                listed.append({"path": r["path"], "bytes": r["bytes"], "lines": 0, "reason": "empty file"})
                continue
            by_tier.setdefault(tier, []).append(r)

    chunks = []
    key = rules.KEY[repo]
    for tier in ("core", "module", "codec", "support", "buildci", "docs"):
        files = by_tier.get(tier, [])
        if not files:
            continue
        for f in files:
            f["group"] = rules.group_key(f["path"])
        # pair include/<stem>.h with the source of the same stem (core tier only)
        ordered = sorted(files, key=lambda f: (f["group"], f["path"]))
        if tier == "core":
            stems = {}
            for f in ordered:
                if not f["path"].startswith("include/"):
                    stem = os.path.splitext(os.path.basename(f["path"]))[0].lower()
                    if os.path.splitext(f["path"])[1].lower() in (".c", ".cpp", ".cc", ".s"):
                        stems.setdefault(stem, []).append(f)
            paired, rest = {}, []
            for f in ordered:
                if f["path"].startswith("include/") and f["path"].count("/") == 1:
                    stem = os.path.splitext(os.path.basename(f["path"]))[0].lower()
                    if len(stems.get(stem, [])) == 1:
                        paired.setdefault(id(stems[stem][0]), []).append(f)
                        continue
                rest.append(f)
            ordered2 = []
            for f in rest:
                for h in paired.get(id(f), []):
                    ordered2.append(dict(h, group=f["group"]))
                ordered2.append(f)
            ordered = ordered2
        packed = pack(ordered, rules.TIER_LIMIT[tier])
        for i, ch in enumerate(packed, 1):
            groups = []
            for f in ch:
                if f["group"] not in groups:
                    groups.append(f["group"])
            label = ", ".join(groups[:3]) + (" +%d more" % (len(groups) - 3) if len(groups) > 3 else "")
            if len(ch) == 1 and ch[0].get("part"):
                label = "%s lines %d-%d" % (ch[0]["path"], ch[0]["start"], ch[0]["end"])
            chunks.append({
                "id": "%s-%s-%02d" % (key, tier, i), "tier": tier, "label": label,
                "lines": sum(f["lines"] for f in ch),
                "files": [{"path": f["path"], "lines": f["lines"], "kind": f["kind"],
                           "start": f["start"], "end": f["end"], "part": bool(f.get("part"))} for f in ch],
            })
    for i, (label, files) in enumerate(sorted(skim.items()), 1):
        chunks.append({
            "id": "%s-skim-%02d" % (key, i), "tier": "skim", "label": label,
            "lines": sum(f["lines"] for f in files),
            "files": [{"path": f["path"], "lines": f["lines"], "kind": f["kind"], "start": 1, "end": f["lines"],
                       "part": False} for f in sorted(files, key=lambda f: f["path"])],
        })

    plan = {"repo": repo, "commit": commit, "chunks": chunks, "listed": listed}
    os.makedirs(os.path.join(root, "plan"), exist_ok=True)
    with open(os.path.join(root, "plan", repo + ".json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(plan, fh, indent=1)

    total_files = len(rows)
    planned = sum(1 for c in chunks for f in c["files"] if not f["part"] or f["start"] == 1)
    print("%s commit %s: %d files -> %d chunks (%d files planned, %d listed)" % (
        repo, commit[:10], total_files, len(chunks), planned, len(listed)))
    tiers = {}
    for c in chunks:
        t = tiers.setdefault(c["tier"], [0, 0, 0])
        t[0] += 1
        t[1] += len(c["files"])
        t[2] += c["lines"]
    for t, (n, nf, nl) in tiers.items():
        print("  %-9s chunks=%-3d files=%-5d lines=%d" % (t, n, nf, nl))
    assert planned + len(listed) == total_files, (planned, len(listed), total_files)
    if "-v" in sys.argv:
        for c in chunks:
            print("   %-18s %6dL %3df  %s" % (c["id"], c["lines"], len(c["files"]), c["label"][:90]))


if __name__ == "__main__":
    main()
