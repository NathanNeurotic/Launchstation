#!/usr/bin/env python3
"""Blocking loops opened under START must yield to a pending Dashboard action."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
widgets = (root / "src" / "dash_widgets.c").read_text(encoding="utf-8")
tools = (root / "src" / "dash_tools.c").read_text(encoding="utf-8")

ipv4 = widgets[widgets.index("// IPv4 address editor"):]
ipv4 = ipv4[:ipv4.index("return result;")]
assert "if (dashDashboardPending() || (in.pressed & (DASH_IN_BACK | DASH_IN_MENU)))" in ipv4
assert "memcpy(oct, orig, sizeof(oct));" in ipv4  # pending action discards edits

picker = widgets[widgets.index("rows: [Use this folder]") - 2000:]
assert "if (dashDashboardPending() || (in.pressed & DASH_IN_MENU))" in picker

link = tools[tools.index("static void cloudLink(void)"):]
link = link[:link.index("\n}\n")]
assert link.index("dashDashboardPending()") < link.index("CLOUD_LINK_EXPIRED")
print("PASS: IPv4 editor, folder picker and Drive link loop yield to pending Dashboard actions")
