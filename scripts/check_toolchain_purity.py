#!/usr/bin/env python3
"""Fail if an LLVM build linked any GCC-compiled object.

Every compiler records itself in the ELF .comment section, and the linker
merges those strings into the output. An LLVM release must therefore contain
only "clang version" producers: a "GCC:" entry means a GCC archive (for
example a mispackaged SDK library) was linked into a Clang build, mixing
code generation and ABI assumptions the toolchain split is meant to keep
apart.
"""

import argparse
import pathlib
import re
import subprocess
import sys


def producers(readobj, elf):
    output = subprocess.check_output(
        [readobj, "--string-dump=.comment", str(elf)], text=True, stderr=subprocess.STDOUT
    )
    found = set()
    for line in output.splitlines():
        match = re.search(r"\]\s+(.*)$", line)
        if match:
            found.add(match.group(1).strip())
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("readobj", help="llvm-readobj (or readelf-compatible) tool")
    parser.add_argument("elves", nargs="+", type=pathlib.Path)
    args = parser.parse_args()

    failed = False
    for elf in args.elves:
        found = producers(args.readobj, elf)
        gcc = sorted(p for p in found if p.startswith("GCC:"))
        clang = sorted(p for p in found if "clang version" in p)
        if gcc or not clang:
            failed = True
            print(f"FAIL {elf}: producers={sorted(found)}", file=sys.stderr)
        else:
            print(f"PASS {elf}: {len(clang)} clang producer(s), no GCC objects")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
