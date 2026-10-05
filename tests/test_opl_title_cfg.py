"""Guard OPL title.cfg parsing semantics used by the dashboard app library."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/dash_lib.c").read_text()

# OPL distinguishes lowercase metadata keys from similarly named descriptive keys.
assert 'm->opl_style && !strcmp(key, "boot")' in src
assert 'm->opl_style && !strcmp(key, "title")' in src
assert '!m->opl_style && (!stricmp(key, "title") || !stricmp(key, "name"))' in src

opl = src[src.index("static int metaKv("):src.index("static void splitPath(", src.index("static int metaKv("))]
assert 'm->opl_style && !stricmp(key, "boot")' not in opl
assert 'm->opl_style && !stricmp(key, "title")' not in opl

print("PASS: OPL title.cfg uses exact lowercase title/boot while wLE sidecars remain case-insensitive")
