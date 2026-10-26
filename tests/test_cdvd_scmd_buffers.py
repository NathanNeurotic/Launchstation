"""sceCdApplySCmd always stores 16 bytes of output; smaller buffers overflow the stack.

A 9-byte buffer in the console-model read was harmless under GCC but fatal under
LLVM once the read moved to startup (black screen on boot). Check every call
site passes a local array of at least 16 bytes (or NULL).
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
sources = sorted((root / "src").glob("*.c"))


def call_args(text, open_paren):
    """Return the top-level comma-separated arguments of the call at open_paren."""
    depth, args, current = 0, [], ""
    for ch in text[open_paren:]:
        if ch == "(":
            depth += 1
            if depth == 1:
                continue
        elif ch == ")":
            depth -= 1
            if depth == 0:
                args.append(current.strip())
                return args
        elif ch == "," and depth == 1:
            args.append(current.strip())
            current = ""
            continue
        current += ch
    raise AssertionError("unterminated call")


checked = 0
for path in sources:
    text = path.read_text(encoding="latin-1")  # some sources are not UTF-8
    for call in re.finditer(r"\bsceCdApplySCmd\s*\(", text):
        args = call_args(text, call.end() - 1)
        assert len(args) == 4, f"{path.name}: unexpected sceCdApplySCmd arguments {args}"
        out = args[3]
        if out == "NULL":
            continue
        name = re.fullmatch(r"&?([A-Za-z_]\w*)", out)
        assert name, f"{path.name}: output buffer must be a plain local array: {out}"
        decls = list(re.finditer(
            rf"\b(?:unsigned\s+char|u8|char)\s+{name.group(1)}\s*\[\s*(\w+)\s*\]", text[:call.start()]))
        assert decls, f"{path.name}: no array declaration found for {out}"
        size = decls[-1].group(1)
        assert size.isdigit(), f"{path.name}: buffer size for {out} must be a literal: {size}"
        assert int(size) >= 16, f"{path.name}: {out}[{size}] is smaller than the 16 bytes sceCdApplySCmd writes"
        checked += 1
assert checked >= 4, f"expected to check the known sceCdApplySCmd call sites, found {checked}"

# sceCdMV can return RR MM mm TT (four bytes).
info = (root / "src/main_console_info.c").read_text(encoding="utf-8")
assert re.search(r"u8\s+mecha_version\s*\[\s*4\s*\]", info), "mecha_version needs room for four bytes"
print(f"PASS: {checked} sceCdApplySCmd call sites use output buffers of at least 16 bytes")
