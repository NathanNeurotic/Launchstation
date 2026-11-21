"""Summarize an inventory TSV by directory depth.

Usage: python -I summ.py <inventory.tsv> <depth> [top_n_files]
"""
import sys
from collections import defaultdict


def main():
    tsv, depth = sys.argv[1], int(sys.argv[2])
    topn = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    rows = []
    with open(tsv, encoding="utf-8") as fh:
        next(fh)
        for line in fh:
            p, b, l, k = line.rstrip("\n").split("\t")
            rows.append((p, int(b), int(l), k))
    agg = defaultdict(lambda: defaultdict(lambda: [0, 0, 0]))
    for p, b, l, k in rows:
        parts = p.split("/")
        key = "/".join(parts[:depth]) if len(parts) > depth else "/".join(parts[:-1]) or "."
        a = agg[key][k]
        a[0] += 1
        a[1] += b
        a[2] += l
    for key in sorted(agg):
        segs = []
        tot = 0
        for k in ("source", "build", "workflow", "doc", "othertext", "binary"):
            if k in agg[key]:
                n, b, l = agg[key][k]
                if k == "binary":
                    segs.append("bin:%d(%dKB)" % (n, b // 1024))
                else:
                    segs.append("%s:%d/%dL" % (k[:3], n, l))
                    tot += l
        print("%-48s %7dL  %s" % (key, tot, " ".join(segs)))
    if topn:
        print("--- largest text files")
        for p, b, l, k in sorted((r for r in rows if r[3] != "binary"), key=lambda r: -r[2])[:topn]:
            print("%7dL %9dB %-9s %s" % (l, b, k, p))


if __name__ == "__main__":
    main()
