"""Guard nested full-screen tools against the Text Editor soft lock.

The File Browser runs its own loop with its own scene and can open the Text
Editor (or HDD Manager/exploit installer through MISC). Leaving the nested
tool used to reset the scene to the dashboard while the browser loop kept
reading input underneath, which looked like a frozen console. Every tool must
push its scene on entry and pop it on exit so the opener's scene returns.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
dash = (root / "src/dash_main.c").read_text(encoding="utf-8")
header = (root / "include/dash_internal.h").read_text(encoding="utf-8")

assert "void dashPushSceneOverride(void (*fn)(void));" in header
assert "void dashPopSceneOverride(void);" in header
assert "dashSetSceneOverride" not in header
assert "scene_override_stack" in dash

for name in ("dash_browser.c", "dash_editor.c", "dash_hdd.c", "dash_exploit.c"):
    source = (root / "src" / name).read_text(encoding="utf-8")
    assert "dashSetSceneOverride" not in source, name
    pushes = len(re.findall(r"dashPushSceneOverride\(\w+Scene\);", source))
    pops = source.count("dashPopSceneOverride();")
    assert pushes == 1, (name, pushes)
    assert pops >= 1, (name, pops)

# The HDD Manager's early exit after a failed scan must pop too.
hdd = (root / "src/dash_hdd.c").read_text(encoding="utf-8")
assert re.search(r"if \(!scan\(\)\) \{\s*dashPopSceneOverride\(\);\s*return;", hdd)

print("PASS: nested full-screen tools restore the scene of the tool that opened them")
