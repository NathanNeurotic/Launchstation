"""Guard the native PNG/JPG/JPEG dashboard image viewer."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
browser = (root / "src/dash_browser.c").read_text()
viewer = (root / "src/dash_image.c").read_text()
jpeg = (root / "src/dash_jpeg.c").read_text()
header = (root / "include/dash_internal.h").read_text()
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
assert "DASH_IN_R3" in viewer
assert "DASH_IN_START" not in viewer
assert "DASH_IN_FAV" in viewer
assert "DASH_IN_L2" in viewer and "DASH_IN_R2" in viewer
assert "PAD_L3_H0" in viewer and "PAD_L3_V1" in viewer
assert "rotation" in viewer
assert "slideshow" in viewer
assert "dashTexDrawQuadUV" in viewer
# Check the actual image draw tint: GS MODULATE unity is 128, not 255.
# Alpha uses the dashboard's 0..255 API and is converted separately.
image_draw = re.search(r"dashTexDrawQuadUV\(tex,[\s\S]*?DC\((\d+), (\d+), (\d+), (\d+)\)\);", viewer)
assert image_draw, "Missing image texture draw tint"
assert tuple(map(int, image_draw.groups())) == (128, 128, 128, 255), "Image tint doubles/clips colors instead of preserving them"
assert "gsKit_prim_quad_texture_3d" in (root / "src/dash_gfx.c").read_text()
for filename in ("dash_games.c", "dash_apps.c", "dash_art.c", "dash_bg.c"):
    artwork = (root / "src" / filename).read_text()
    draws = re.findall(r"dashTexDraw(?:Fit)?\([\s\S]*?DC\((\d+), (\d+), (\d+), (\d+)\)\);", artwork)
    assert draws, f"Missing decoded artwork draw in {filename}"
    assert all(tuple(map(int, draw)) == (128, 128, 128, 255) for draw in draws), \
        f"{filename} doubles/clips decoded artwork colors: {draws}"
# Focus wallpaper uses a separate cached-art path; both frames must retain
# the same neutral GS modulation without changing their fade alpha.
background = (root / "src/dash_bg.c").read_text()
for cached in ("prev", "cur"):
    assert f"drawCover({cached}, DC(128, 128, 128, (int)(" in background, (
        f"Focus wallpaper {cached} frame clips the artwork colors")

assert "jpegturbo" in ps2yaml
assert "src/dash_image.c" in ps2yaml
assert "src/dash_jpeg.c" in ps2yaml
assert "jpeg" in ps2yaml
assert 'genCmpFileExt(e->name, "JPG")' in widgets
assert 'genCmpFileExt(e->name, "JPEG")' in widgets

print("PASS: PNG/JPG/JPEG viewer supports bounded decoding, browse, rotate, zoom/pan, fullscreen and slideshow")
