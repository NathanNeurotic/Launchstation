"""Edit metadata must create <ELFNAME>.cfg even when only an OPL title.cfg describes the app."""
from pathlib import Path
src = (Path(__file__).resolve().parents[1] / "src/dash_apps.c").read_text()
a = src.index("static void editMetadata")
body = src[a:src.index("\n}\n", a)]
assert "sidecarExists(meta)" in body, "must test the sidecar file itself"
assert "DASH_APP_HAS_META" not in body, "HAS_META is also set by title.cfg alone"
assert src.index("static int sidecarExists") < src.index("sidecarExists(meta)"), "defined before first use"
print("PASS: Edit metadata keys off the real <ELFNAME>.cfg, not the title.cfg-inclusive flag")
# Comment edits must not eat code: the starter block still declares its buffer and length.
assert "char text[512];" in body and "int len = snprintf(text" in body
assert "*/ sidecar" not in body
