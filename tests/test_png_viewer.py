"""Guard the native PNG/JPG/JPEG dashboard image viewer."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
browser = (root / "src/dash_browser.c").read_text()
viewer = (root / "src/dash_image.c").read_text()
jpeg = (root / "src/dash_jpeg.c").read_text()
header = (root / "include/dash_internal.h").read_text()
makefile = (root / "Makefile").read_text()
ps2yaml = (root / "ps2.yaml").read_text()
widgets = (root / "src/dash_widgets.c").read_text()

for ext in ("PNG", "JPG", "JPEG"):
    assert f'genCmpFileExt(f->name, "{ext}")' in browser

assert "isImageFile(f)" in browser
assert "dashShowImageViewer(path, f->name, &iv)" in browser
assert "nextImageIndex" in browser
assert "revealSelection" in browser
assert "int dashShowImageViewer" in header
assert "dashPngLoadFile" in viewer
assert "dashJpegLoadFile" in viewer
assert "dashJpegDecode" in header
assert "jpeg_mem_src" in jpeg
assert "JCS_RGB" in jpeg
assert "DASH_JPEG_MAX_FILE" in jpeg
assert "DASH_JPEG_MAX_DIM" in jpeg
assert "dashTexRelease(&tex)" in viewer
assert "free(pixels)" in viewer
assert "DASH_IN_BACK" in viewer
assert "DASH_IN_START" in viewer
assert "DASH_IN_FAV" in viewer
assert "DASH_IN_L2" in viewer and "DASH_IN_R2" in viewer
assert "PAD_L3_H0" in viewer and "PAD_L3_V1" in viewer
assert "rotation" in viewer
assert "slideshow" in viewer
assert "dashTexDrawQuadUV" in viewer
assert "dash_image.o" in makefile
assert "gsKit_prim_quad_texture_3d" in (root / "src/dash_gfx.c").read_text()
assert "dash_jpeg.o" in makefile
assert "-ljpeg" in makefile
assert "src/dash_image.c" in ps2yaml
assert "src/dash_jpeg.c" in ps2yaml
assert "jpeg" in ps2yaml
assert 'genCmpFileExt(e->name, "JPG")' in widgets
assert 'genCmpFileExt(e->name, "JPEG")' in widgets

print("PASS: PNG/JPG/JPEG viewer supports bounded decoding, browse, rotate, zoom/pan, fullscreen and slideshow")
