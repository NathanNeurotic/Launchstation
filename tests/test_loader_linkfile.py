"""Regression guard for newer PS2BUILD startup and the low-memory loader.

Do not bypass startup symbols by hardcoding zero addresses or changing the
loader's existing bram/kram allocation.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
linker = (root / "loader/linkfile").read_text()
workflow = (root / ".github/workflows/compile.yml").read_text()
project = (root / "ps2.yaml").read_text()

assert "ORIGIN = 0x0084000, LENGTH = 0x1c000" in linker
assert "ORIGIN = 0x0080000, LENGTH = 0x1500" in linker
for name in ("preinit_array", "init_array", "fini_array"):
    assert f".{name} ALIGN(16):" in linker
    assert f"__{name}_start" in linker
    assert f"__{name}_end" in linker
    assert f"KEEP(*(.{name}))" in linker
    assert linker.index(f"__{name}_start") < linker.index(f"__{name}_end")
    assert linker.index(f".{name} ALIGN(16)") < linker.index(".reginfo")
assert 'linkfile: loader/linkfile' in project
assert "python3 tests/test_loader_linkfile.py" in workflow
print("PASS: startup arrays remain defined inside the loader's low-memory window")
