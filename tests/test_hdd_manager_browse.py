"""HDD Manager regression: bounds on 4:3 detail panel and on-demand HDL RPCs.

A long APA identifier (PP.SLUS-... etc.) was centered without a width
constraint, drawing through the edge of the 244px details card. Continuous
title lookup on every 180ms selection pause also changed classic browsing
labels and could cause synchronous HDL RPC delays during scrolling.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_hdd.c").read_text(encoding="utf-8")


def function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, pos = 1, brace + 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


details = function("static void drawDetails(")
scene = function("static void hddScene(")
manager = function("void dashHddManager(")
rename = function("static void actRenameGame(")

# Every long, dynamic headline/line in the narrow details card is width
# limited. Avoid unbounded centered names and startup IDs.
assert '#define DET_W 244.0f' in source
assert "dashTextFit(x + 16, y + 72, w - 32, p->name" in details
assert "dashTextFit(x + 16, y + 98, w - 32, typeName(p)" in details
assert "dashTextFit(x + 16, y + 122, w - 32, line" in details
assert "dashTextFit(x + 16, y + 44, w - 32, line" in details
assert "dashTextWrap(x + 16, y, w - 32, 2, p->game.Name" in details
assert "dashTextCenter(x + w * 0.5f, y + 72, p->name" not in details
assert "dashTextCenter(x + w * 0.5f, y + 44, line" not in details
assert "dashTextFit(40, 78, 560, line" in scene

# Display APA identifiers, not titles, while navigating the partition list.
assert "const char *label = p->name;" in scene
assert "const char *label = (p->type == HP_HDL" not in scene
assert "dashTextFit(LIST_X + 30, y + 3, LIST_W - 110, label" in scene

# Idle browsing may read PFS usage but must not perform synchronous HDL
# title queries. R1 and the explicit Rename game action may request them.
idle = manager[manager.index("if (hm.n > 0 && Timer() - last_move > 180)"):
               manager.index("sel_anim = dashApproach(", manager.index("if (hm.n > 0 && Timer() - last_move > 180)"))]
assert "loadPfsInfo(sel);" in idle
assert "loadGameInfo(" not in idle
assert "DASH_IN_R1" in manager
assert "loadGameInfo(&hm.parts[sel]);" in manager
assert "loadGameInfo(p);" in rename
assert '"Game info"' in scene
assert '"Press R1 for game information"' in details

print("PASS: APA IDs stay visible; bounded 4:3 detail text; HDL RPC only on demand")
