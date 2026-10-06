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

assert "About r3LaunchELF_ATTDASH" in main_info
assert '"About r3LaunchELF_ATTDASH"' in tools
assert "r3LaunchELF_ATTDASH %s" in draw
assert "r3LaunchELF_ATTDASH cannot open this type of file" in browser
assert "cannot be formatted from r3LaunchELF_ATTDASH" in hdd
assert "# r3LaunchELF_ATTDASH dashboard metadata" in apps
assert "User-Agent: r3LaunchELF_ATTDASH/Cloud" in cloud

assert "r3LaunchELF_ATTDASH issue report" in issue
assert "NathanNeurotic/r3LaunchELF_ATTDASH/issues" in issue
assert "wLaunchELF 4.43x_isr" not in issue
assert "israpps/wLaunchELF_ISR/issues" not in issue

# Rebranding must never erase the project's direct-base attribution.
assert "wLaunchELF_R3Z" in readme
assert "Direct base: wLaunchELF_R3Z" in credits
