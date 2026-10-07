"""Guard the native PNG/JPG/JPEG dashboard image viewer."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
browser = (root / "src/dash_browser.c").read_text()
viewer = (root / "src/dash_image.c").read_text()
jpeg = (root / "src/dash_jpeg.c").read_text()
header = (root / "include/dash_internal.h").read_text()
makefile = (root / "Makefile").read_text()
ps2yaml = (root / "ps2.yaml").read_text()

for ext in ("PNG", "JPG", "JPEG"):
    assert f'genCmpFileExt(path, "{ext}")' in browser

assert "dashShowImageViewer(path, f->name)" in browser
assert "void dashShowImageViewer" in header
assert "dashPngLoadFile" in viewer
assert "dashJpegLoadFile" in viewer
assert "dashJpegDecode" in header
assert "jpeg_mem_src" in jpeg
assert "JCS_RGB" in jpeg
assert "DASH_JPEG_MAX_FILE" in jpeg
assert "DASH_JPEG_MAX_DIM" in jpeg
assert "dashTexDrawFit" in viewer
assert "dashTexRelease(&tex)" in viewer
assert "free(pixels)" in viewer
assert "DASH_IN_BACK" in viewer
assert "dash_image.o" in makefile
assert "dash_jpeg.o" in makefile
assert "-ljpeg" in makefile
assert "src/dash_image.c" in ps2yaml
assert "src/dash_jpeg.c" in ps2yaml
assert "jpeg" in ps2yaml

print("PASS: PNG/JPG/JPEG files use the bounded native dashboard image viewer")
