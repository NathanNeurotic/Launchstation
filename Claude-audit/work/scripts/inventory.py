"""Inventory tracked files of a git clone: path, bytes, lines, kind.

Usage: python -I inventory.py <repo_dir> <out_tsv>
Read-only: only runs `git ls-files` and reads file bytes.
"""
import os
import subprocess
import sys

SRC_EXT = {
    ".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".hh", ".s", ".S", ".asm", ".inc",
    ".py", ".go", ".cs", ".js", ".ts", ".lua", ".sh", ".bat", ".ps1", ".cmd",
    ".vcl", ".vsm", ".dsm", ".java", ".rs", ".pl", ".tcl",
}
BUILD_EXT = {
    ".mk", ".mak", ".cmake", ".yml", ".yaml", ".toml", ".json", ".ld", ".lds",
    ".lst", ".tab", ".cnf", ".cfg", ".ini", ".spec", ".in", ".sum", ".mod",
    ".env", ".service", ".patch", ".diff", ".xml", ".natvis", ".iss", ".nsi",
    ".props", ".targets", ".sln", ".vcxproj", ".csproj", ".manifest", ".rc",
    ".def", ".map", ".gitignore", ".gitattributes", ".editorconfig",
    ".clang-format", ".clangd", ".dockerignore",
}
BUILD_NAMES = {
    "Makefile", "makefile", "GNUmakefile", "CMakeLists.txt", "Dockerfile",
    "Makefile.pref", "Makefile.eeglobal", "Makefile.iopglobal", "Defs.make",
    "Rules.make", "go.mod", "go.sum", "requirements.txt",
}
DOC_EXT = {".md", ".txt", ".html", ".htm", ".css", ".rst", ".url", ".tsv", ".csv"}


def classify(path, is_binary):
    base = os.path.basename(path)
    ext = os.path.splitext(base)[1]
    if is_binary:
        return "binary"
    if path.startswith(".github/workflows/"):
        return "workflow"
    if base in BUILD_NAMES or base.startswith("Makefile"):
        return "build"
    if ext in SRC_EXT or ext.lower() in SRC_EXT:
        return "source"
    if ext in BUILD_EXT or ext.lower() in BUILD_EXT or base in BUILD_EXT:
        return "build"
    if ext.lower() in DOC_EXT:
        return "doc"
    return "othertext"


def main():
    repo, out = sys.argv[1], sys.argv[2]
    files = subprocess.run(
        ["git", "-C", repo, "ls-files", "-z"], capture_output=True, check=True
    ).stdout.decode("utf-8", "surrogateescape").split("\0")
    files = [f for f in files if f]
    rows = []
    for f in files:
        p = os.path.join(repo, f)
        try:
            with open(p, "rb") as fh:
                data = fh.read()
        except OSError as e:
            rows.append((f, -1, -1, "unreadable:" + type(e).__name__))
            continue
        head = data[:8192]
        is_binary = b"\0" in head
        lines = 0 if is_binary else data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)
        rows.append((f, len(data), lines, classify(f, is_binary)))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("path\tbytes\tlines\tkind\n")
        for r in rows:
            fh.write("%s\t%d\t%d\t%s\n" % r)
    kinds = {}
    for _, b, l, k in rows:
        n, bb, ll = kinds.get(k, (0, 0, 0))
        kinds[k] = (n + 1, bb + max(b, 0), ll + max(l, 0))
    print(os.path.basename(repo.rstrip("/\\")), "files=%d" % len(rows))
    for k in sorted(kinds):
        print("  %-10s files=%-5d bytes=%-11d lines=%d" % ((k,) + kinds[k]))


if __name__ == "__main__":
    main()
