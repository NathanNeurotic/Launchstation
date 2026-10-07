"""Guard native PNG viewing from the dashboard File Browser."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
browser = (root / "src/dash_browser.c").read_text()
viewer = (root / "src/dash_image.c").read_text()
header = (root / "include/dash_internal.h").read_text()
makefile = (root / "Makefile").read_text()
ps2yaml = (root / "ps2.yaml").read_text()

assert 'genCmpFileExt(path, "PNG")' in browser
assert "dashShowPngViewer(path, f->name)" in browser
assert "void dashShowPngViewer" in header
assert "dashPngLoadFile" in viewer
assert "dashTexDrawFit" in viewer
assert "dashTexRelease(&tex)" in viewer
assert "free(pixels)" in viewer
assert "DASH_IN_BACK" in viewer
assert "dash_image.o" in makefile
assert "src/dash_image.c" in ps2yaml

print("PASS: PNG files open in the native dashboard viewer and release their texture memory")
