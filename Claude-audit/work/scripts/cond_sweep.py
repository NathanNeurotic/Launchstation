"""Conditional-compilation and leftover sweep for one repo (evidence only, candidates not verdicts).

Usage: python -I cond_sweep.py <work_root> <Repo>
Outputs under evidence/<Repo>/ :
  cond-if0.tsv               #if 0 blocks and dead #else branches of #if 1
  cond-macros-undefined.tsv  macros tested by the preprocessor but never #defined or passed with -D in the repo
  cond-commented-code.tsv    runs of commented-out code
  cond-markers.tsv           TODO / FIXME / XXX / HACK markers
"""
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rules  # noqa: E402

C_EXT = {".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".hh", ".s", ".inc"}
SLASH_EXT = C_EXT | {".go", ".js", ".ts", ".java", ".cs"}
PP = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif|define|undef)\b(.*)$")
IDENT = re.compile(r"[A-Za-z_]\w*")
DEFINED = re.compile(r"defined\s*\(?\s*([A-Za-z_]\w*)")
DASH_D = re.compile(r"(?<![\w-])-D\s?([A-Za-z_]\w*)")
MARK = re.compile(r"\b(TODO|FIXME|XXX|HACK|WIP|KLUDGE|TEMP HACK|DEBUG ONLY|REMOVE ME|REMOVEME)\b")
BUILTIN = re.compile(r"^(__\w+|_[A-Z]\w*|NDEBUG|WIN32|_WIN32|linux|unix|__cplusplus)$")

C_CODE = re.compile(r"(;|\{|\})\s*$|^\s*(if|for|while|switch|return|else|case|goto)\b.*[;{)]\s*$|^\s*#\s*(include|define|if|endif)")
PY_CODE = re.compile(r"^\s*(\w[\w.\[\]'\"]*\s*(=|\+=|-=)\s*\S.*|(if|elif|for|while|with|try|except|def|class)\b.*:\s*$|"
                     r"(return|import|from|raise|assert|print)\b.*|\w[\w.]*\(.*\)\s*)$")
LUA_CODE = re.compile(r"^\s*(local\s+\w+.*|if\s.+then|for\s.+do|while\s.+do|end|return\b.*|\w[\w.:]*\(.*\)\s*|"
                      r"\w[\w.\[\]]*\s*=\s*\S.*)$")


def main():
    root, repo = sys.argv[1], sys.argv[2]
    src = os.path.join(root, "src", repo)
    out = os.path.join(root, "evidence", repo)
    os.makedirs(out, exist_ok=True)
    rows = []
    with open(os.path.join(root, "inventory", repo + ".tsv"), encoding="utf-8") as fh:
        next(fh)
        for line in fh:
            p, b, l, k = line.rstrip("\n").split("\t")
            tier, _ = rules.classify(repo, p, k)
            if k == "binary" or tier == "list":
                continue
            rows.append((p, k, tier))

    if0, commented, markers = [], [], []
    tested = defaultdict(list)       # macro -> [(path, line)]
    defined_src = defaultdict(int)
    defined_d = defaultdict(int)
    build_mentions = defaultdict(int)
    tiers_of = defaultdict(set)

    for path, kind, tier in rows:
        ext = os.path.splitext(path)[1]
        extl = ext.lower()
        try:
            with open(os.path.join(src, path), "rb") as fh:
                lines = fh.read().decode("utf-8", "replace").split("\n")
        except OSError:
            continue
        is_c = extl in C_EXT
        is_build = kind in ("build", "workflow") or os.path.basename(path).startswith("Makefile") or extl in (
            ".mk", ".yml", ".yaml", ".toml", ".cmake", ".sh", ".py", ".bat", ".ps1")
        # ---- preprocessor
        if is_c:
            stack = []  # (kind, start_line, in_dead_branch_start)
            for n, line in enumerate(lines, 1):
                m = PP.match(line)
                if not m:
                    continue
                d, rest = m.group(1), m.group(2)
                rest_nc = re.sub(r"/\*.*?\*/|//.*$", "", rest).strip()
                if d == "define":
                    mm = IDENT.match(rest_nc)
                    if mm:
                        defined_src[mm.group(0)] += 1
                elif d in ("ifdef", "ifndef"):
                    mm = IDENT.match(rest_nc)
                    if mm:
                        tested[mm.group(0)].append((path, n))
                        tiers_of[mm.group(0)].add(tier)
                    stack.append(["cond", n, None])
                elif d == "if":
                    if rest_nc in ("0", "(0)"):
                        stack.append(["if0", n, n])
                    elif rest_nc in ("1", "(1)"):
                        stack.append(["if1", n, None])
                    else:
                        stack.append(["cond", n, None])
                        names = set(DEFINED.findall(rest_nc)) | {
                            t for t in IDENT.findall(rest_nc) if t != "defined" and not t[0].isdigit()}
                        for t in names:
                            tested[t].append((path, n))
                            tiers_of[t].add(tier)
                elif d == "elif":
                    names = set(DEFINED.findall(rest_nc)) | {
                        t for t in IDENT.findall(rest_nc) if t != "defined"}
                    for t in names:
                        tested[t].append((path, n))
                        tiers_of[t].add(tier)
                    if stack and stack[-1][0] == "if0" and stack[-1][2]:
                        if0.append((path, stack[-1][2], n - 1, n - stack[-1][2], "#if 0 (until #elif)", tier))
                        stack[-1][2] = None
                elif d == "else":
                    if stack:
                        if stack[-1][0] == "if0" and stack[-1][2]:
                            if0.append((path, stack[-1][2], n - 1, n - stack[-1][2], "#if 0 (until #else)", tier))
                            stack[-1][2] = None
                        elif stack[-1][0] == "if1":
                            stack[-1][2] = n
                elif d == "endif":
                    if stack:
                        k2, s, dead = stack.pop()
                        if k2 == "if0" and dead:
                            if0.append((path, dead, n, n - dead + 1, "#if 0", tier))
                        elif k2 == "if1" and dead:
                            if0.append((path, dead, n, n - dead + 1, "#else branch of #if 1", tier))
        # ---- -D definitions and build mentions
        if is_build or is_c:
            for line in lines:
                if "-D" in line:
                    for t in DASH_D.findall(line):
                        defined_d[t] += 1
        if is_build and not is_c:
            seen = set()
            for line in lines:
                for t in IDENT.findall(line):
                    if t not in seen and t.isupper() and len(t) > 2:
                        seen.add(t)
            for t in seen:
                build_mentions[t] += 1
        # ---- markers
        for n, line in enumerate(lines, 1):
            if MARK.search(line):
                markers.append((path, n, tier, line.strip()[:160].replace("\t", " ")))
        # ---- commented-out code
        if extl in SLASH_EXT or ext == ".S":
            prefix, rx = "//", C_CODE
        elif extl == ".py":
            prefix, rx = "#", PY_CODE
        elif extl == ".lua":
            prefix, rx = "--", LUA_CODE
        else:
            prefix = None
        if prefix:
            run_start, run_n, run_code, sample = None, 0, 0, ""
            for n, line in enumerate(lines + [""], 1):
                s = line.strip()
                body = None
                if s.startswith(prefix) and not s.startswith("#!") and not (prefix == "#" and s.startswith("# -*-")):
                    body = s[len(prefix):].lstrip("/-# ").rstrip()
                if body is not None and (body == "" or rx.search(body)):
                    if run_start is None:
                        run_start, run_n, run_code, sample = n, 0, 0, body
                    run_n += 1
                    if body:
                        run_code += 1
                else:
                    if run_start is not None and run_code >= 3:
                        commented.append((path, run_start, n - 1, run_code, tier, sample[:120].replace("\t", " ")))
                    run_start = None
        if is_c:
            # block comments that look like code
            in_block, bstart, bcode, btotal = False, 0, 0, 0
            for n, line in enumerate(lines, 1):
                s = line.strip()
                if not in_block:
                    if s.startswith("/*") and "*/" not in s:
                        in_block, bstart, bcode, btotal = True, n, 0, 0
                else:
                    if "*/" in s:
                        in_block = False
                        if btotal >= 4 and bcode * 2 >= btotal and bcode >= 4:
                            commented.append((path, bstart, n, bcode, tier, "block comment containing code-like lines"))
                    else:
                        btotal += 1
                        if re.search(r"(;|\{|\})\s*$", s):
                            bcode += 1

    def write(name, header, data):
        with open(os.path.join(out, name), "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\t".join(header) + "\n")
            for r in data:
                fh.write("\t".join(str(x) for x in r) + "\n")

    if0.sort(key=lambda r: -r[3])
    write("cond-if0.tsv", ("path", "start", "end", "lines", "kind", "tier"), if0)
    commented.sort(key=lambda r: -r[3])
    write("cond-commented-code.tsv", ("path", "start", "end", "code_like_lines", "tier", "sample"), commented)
    write("cond-markers.tsv", ("path", "line", "tier", "text"), markers)
    und = []
    for m, sites in tested.items():
        if defined_src.get(m) or defined_d.get(m):
            continue
        und.append((m, len(sites), build_mentions.get(m, 0), "builtin-like" if BUILTIN.match(m) else "",
                    ",".join(sorted(tiers_of[m])), "; ".join("%s:%d" % s for s in sites[:6])))
    # first-party, non-builtin macros first; macros tested only inside vendored (skim) code last
    und.sort(key=lambda r: (r[4] == "skim", r[3] != "", -r[1]))
    write("cond-macros-undefined.tsv",
          ("macro", "test_sites", "build_file_mentions", "class", "tiers", "sample_sites"), und)
    print("%s: if0 blocks=%d (%d lines) undefined-tested macros=%d (non-builtin %d) commented-code runs=%d markers=%d" % (
        repo, len(if0), sum(r[3] for r in if0), len(und), sum(1 for r in und if not r[3]), len(commented), len(markers)))


if __name__ == "__main__":
    main()
