"""Guard the fork's public identity while preserving explicit upstream credit."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]

main_info = (root / "src/main_info_screens.c").read_text(encoding="utf-8")
tools = (root / "src/dash_tools.c").read_text(encoding="utf-8")
draw = (root / "src/draw.c").read_text(encoding="utf-8")
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
hdd = (root / "src/dash_hdd.c").read_text(encoding="utf-8")
apps = (root / "src/dash_apps.c").read_text(encoding="utf-8")
cloud = (root / "src/cloud_http.c").read_text(encoding="utf-8")
issue = (root / ".github/ISSUE_TEMPLATE/issue.yml").read_text(encoding="utf-8")
readme = (root / "README.md").read_text(encoding="utf-8")
credits = (root / "src/dash_info.c").read_text(encoding="utf-8")

assert "About Launchstation" in main_info
assert '"About Launchstation"' in tools
assert "Launchstation %s" in draw
assert "Launchstation cannot open this type of file" in browser
assert "cannot be formatted from Launchstation" in hdd
assert "# Launchstation dashboard metadata" in apps
assert "User-Agent: Launchstation/Cloud" in cloud

assert "Launchstation issue report" in issue
assert "NathanNeurotic/Launchstation/issues" in issue
assert "wLaunchELF 4.43x_isr" not in issue
assert "israpps/wLaunchELF_ISR/issues" not in issue

# Rebranding must never erase the project's direct-base attribution.
assert "wLaunchELF_R3Z" in readme
assert "Direct base: wLaunchELF_R3Z" in credits

# Validate the docs website, current repository path, and tab icon.
manifest = (root / "docs-site/manifest.json").read_text(encoding="utf-8")
logo = (root / "docs-site/static/assets/favicon.svg").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert '"brand": "Launchstation"' in manifest
assert "NathanNeurotic/Launchstation" in manifest
assert "nathanneurotic.github.io/Launchstation/" in manifest
assert 'viewBox="0 0 64 64"' in logo
assert 'aria-label="Launchstation PS2 towers and orbs"' in logo
# The rolling release publishes the Launchstation-named app packages.
for asset in ("APP_LAUNCHSTATION.psu", "APP_LAUNCHSTATION.zip",
              "APP_LAUNCHSTATION-DS.psu", "APP_LAUNCHSTATION-DS.zip"):
    assert f"            {asset}\n" in workflow, asset
assert "About r3LaunchELF_ATTDASH" not in main_info
