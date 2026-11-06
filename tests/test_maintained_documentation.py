"""Keep maintained build, release, language, and attribution documentation aligned."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
building = (root / "docs/BUILDING.md").read_text(encoding="utf-8")
building_site = (root / "docs-site/content/building.html").read_text(encoding="utf-8")
contributing = (root / "docs-site/content/contributing.html").read_text(encoding="utf-8")
languages = (root / "docs-site/content/languages.html").read_text(encoding="utf-8")
english = (root / "Lang/ENG.LNG").read_text(encoding="utf-8")

for text in (building, building_site):
    assert "scripts/ps2build_sdk.sh --llvm" in text
    assert ".github/ps2build-sdk.env" in text
    assert "CI always tracks the latest SDK" not in text
    assert "https://ps2build.dev/" in text

assert "re-downloads each published asset" not in contributing
assert "GitHub-computed SHA-256 digest" in contributing
assert "does not re-download every asset" in contributing

indices = [int(value) for value in re.findall(r"^lang\((\d+),", english, re.MULTILINE)]
maximum = max(indices)
assert f"0&ndash;{maximum}" in languages
assert f"468&ndash;{maximum}" in languages
assert "Dashboard_" in languages
assert "Exploit_Folder_" in languages

for name in (
    "README.md",
    "CHANGELOG.md",
    "docs/BUILDING.md",
    "docs/THIRD_PARTY_CREDITS.md",
    "docs-site/content/building.html",
    "docs-site/content/credits.html",
):
    text = (root / name).read_text(encoding="utf-8")
    assert "https://ps2build.dev/" in text, name
    assert "https://git.techwritescode.dev/ps2/ps2build" not in text, name
    assert "https://ps2.techwritescode.dev/" not in text, name

credits = (root / "docs/THIRD_PARTY_CREDITS.md").read_text(encoding="utf-8")
site_credits = (root / "docs-site/content/credits.html").read_text(encoding="utf-8")
for text in (credits, site_credits):
    assert "https://github.com/techwritescode" in text

print(f"PASS: maintained docs match the pinned SDK, release checks, ps2build credits, and language index {maximum}")
