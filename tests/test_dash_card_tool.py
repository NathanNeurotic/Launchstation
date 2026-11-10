"""Card contents tool: the real src/dash_card_tool.c driven through scripted dialogs.

Built with the real card viewer and writer and POSIX stand-ins for the PS2
file calls (tests/dash_card_tool_host.c). Checks what reaches the cards: the
save lands or goes, a verified safety copy is taken before every write and
pruned to ten, the card the MMCE device is serving is never written, refusals
cost no safety copy, failures are reported, and no two files are ever open.
"""
from pathlib import Path
import atexit
import hashlib
import random
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent))
import mc_fixture as fx

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_dash_card_tool.py" in workflow
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
assert '"Card contents..."' in browser and "dashCardTool(path)" in browser
assert "src/dash_card_tool.c" in (root / "ps2.yaml").read_text(encoding="utf-8")
assert "src/mmce_card_switch.c" in (root / "ps2.yaml").read_text(encoding="utf-8")

tmp = Path(tempfile.mkdtemp(prefix="card-tool-"))
atexit.register(shutil.rmtree, tmp, ignore_errors=True)   # ~700 MB of card images per run
tool, lister = tmp / "tool", tmp / "list"
flags = ["gcc", "-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
         "-fno-sanitize-recover=all"]
subprocess.run(flags + [f"-I{root/'tests/card-tool-shim'}", f"-I{root/'include'}",
                        str(root / "tests/dash_card_tool_host.c"), str(root / "src/dash_card_tool.c"),
                        str(root / "src/dash_ps1_card.c"), str(root / "src/mc_ps1_card.c"),
                        str(root / "src/mmce_card_switch.c"),
                        str(root / "src/mc_card_write.c"), str(root / "src/mc_card_view.c"),
                        str(root / "src/mc_sha256.c"), str(root / "src/mc_save_formats.c"),
                        str(root / "src/mc_save_export.c"), "-lmbedcrypto", "-lz", "-o", str(tool)],
               check=True)
subprocess.run(flags + [f"-I{root/'include'}", str(root / "tests/mc_card_write_host.c"),
                        str(root / "src/mc_card_write.c"), str(root / "src/mc_card_view.c"),
                        str(root / "src/mc_sha256.c"), "-o", str(lister)], check=True)

dev = tmp / "dev"
(dev / "mass").mkdir(parents=True)
(dev / "mmce0/MemoryCards/Card1").mkdir(parents=True)
(dev / "mmce0/MemoryCards/Card2").mkdir(parents=True)
(dev / "mass/out").mkdir()


def host(path):
    d, rest = path.split(":", 1)
    return dev / d / rest.lstrip("/")


def run(image, *script, active="mmce0:/MemoryCards/Card9/none.mcd", env=None):
    import os
    e = dict(os.environ, **(env or {}))
    out = subprocess.run([str(tool), str(dev), "auto" if active == "auto" else str(host(active)), image, *script], capture_output=True, env=e,
                         timeout=120)
    text = out.stdout.decode("latin-1")
    assert out.returncode == 0, (script, out.returncode, text[-1500:], out.stderr.decode("latin-1")[-1500:])
    lines = text.splitlines()
    end = lines[-1]
    assert end.startswith("END|open=0|max_open="), end
    assert int(end.split("max_open=")[1].split("|")[0]) <= 1
    return lines


def saves(path):
    """{save: {file: sha}} via the writer harness's independent listing."""
    out = subprocess.run([str(lister), "list", str(host(path))], capture_output=True, text=True, check=True)
    lines = out.stdout.splitlines()
    assert lines[0] == "open=0", lines
    got = {}
    for l in lines[3:]:
        p = l.split("|")
        assert p[2] == "0", l
        got[p[1]] = tuple(sorted(p[4:]))
    return got


def put(path, card):
    host(path).write_bytes(card.image())


def blank(path):
    put(path, fx.Card(8, False).finish())


def sha(path):
    return hashlib.sha256(host(path).read_bytes()).hexdigest()


def kind(lines, prefix):
    return [l for l in lines if l.startswith(prefix)]


std, _ = fx.standard_card(8, False)
names = ["BESLES-12345GAME", "BASLUS-99999", "BISLPS-00001", "NOICON"]

# 1. listing: import row, then one row per save, titles from icon.sys, free space
A = "mass:/A.mcd"
put(A, std)
lines = run(A)
menu = kind(lines, "MENU|A.mcd|")[0].split("|")
assert menu[2].startswith("4 saves, ") and " KB free of " in menu[2], menu[2]
assert menu[3].startswith("Import save...")
assert menu[4].startswith("Copy to a new card of another size...")
assert menu[5].startswith("Split into Game ID cards...")
assert menu[6].startswith("T:Game Slot 1~BESLES-12345GAME, 3 files, ")
assert menu[8].startswith("BISLPS-00001~PS1 BISLPS-00001, 2 files, 1 KB")
assert menu[9].startswith("NOICON~NOICON, 2 files, 3 KB")

# 2. copy A -> B: the save lands, B's old bytes are in WLE-Safety, A is untouched
B = "mass:/B.mcd"
blank(B)
b_before, a_before = host(B).read_bytes(), host(A).read_bytes()
lines = run(A, "m3", "m0", "p" + B, "c1")
assert kind(lines, "TOAST|1|Save copied and verified"), lines
assert saves(B) == {names[0]: saves(A)[names[0]]}
assert host("mass:/WLE-Safety/B-0001.mcd").read_bytes() == b_before
assert host(A).read_bytes() == a_before
pick = kind(lines, "PICK|")[0].split("|")
assert pick[2] == "mass:/" and pick[3] == "0"

# 3. refusals cost nothing: already there, not a card, the same card, declined
for script, needle in ((("m3", "m0", "p" + B, "c1"), "already on that card"),
                       (("m3", "m0", "p" + A), "already on this card"),
                       (("m4", "m0", "p" + B, "c0"), None)):
    snap = {p: sha(p) for p in (A, B)}
    lines = run(A, *script)
    if needle:
        assert any(needle in l for l in kind(lines, "MESSAGE|")), (script, lines)
    assert {p: sha(p) for p in (A, B)} == snap
    assert not kind(lines, "SAFETY|")
host("mass:/junk.mcd").write_bytes(b"\0" * 8192)
lines = run(A, "m3", "m0", "pmass:/junk.mcd", "c1")
assert any("not a PS2 memory card" in l for l in kind(lines, "MESSAGE|")) and not kind(lines, "SAFETY|")

# 4. the card the MMCE device serves is never written; unknown also refuses
C1, C2 = "mmce0:/MemoryCards/Card1/Card1-1.mcd", "mmce0:/MemoryCards/Card2/Card2-1.mcd"
blank(C1)
c = fx.Card(8, False)
c.add_save("OTHER", {"x": b"q" * 5000})
put(C2, c.finish())
snap = sha(C1)
lines = run(A, "m4", "m0", "p" + C1, active=C1)
assert any("This card is in use" in l for l in kind(lines, "MESSAGE|")) and sha(C1) == snap
lines = run(A, "m4", "m0", "p" + C1, active="mmce0:/MemoryCards/Card9/missing.mcd")
assert any("could not read this card or the card the MMCE device is serving" in l for l in kind(lines, "MESSAGE|"))
assert sha(C1) == snap and not kind(lines, "SAFETY|")
lines = run(C1, "m0", active=C1)                                # import on the served card
assert any("This card is in use" in l for l in kind(lines, "MESSAGE|"))
lines = run(A, "m4", "m0", "p" + C1, "c1", active=C2)            # another card is served: allowed
assert kind(lines, "TOAST|1|Save copied"), lines
assert kind(lines, "SAFETY|mmce0:/WLE-Safety/Card1-1-0001.mcd|1")
assert names[1] in saves(C1)

# 5. move: lands there, then leaves here; both cards get a safety copy
blank("mass:/D.mcd")
a_before = host(A).read_bytes()
lines = run(A, "m6", "m1", "pmass:/D.mcd", "c1")
assert kind(lines, "TOAST|1|Save moved and verified"), lines
assert names[3] in saves("mass:/D.mcd") and names[3] not in saves(A)
assert host("mass:/WLE-Safety/A-0001.mcd").read_bytes() == a_before
assert host("mass:/WLE-Safety/D-0001.mcd").exists()

# 6. a move whose source is the served card is refused before anything is written
put(C2, std)
snap = (sha(C2), sha(B))
lines = run(C2, "m5", "m1", "p" + B, active=C2)
assert any("This card is in use" in l for l in kind(lines, "MESSAGE|")) and (sha(C2), sha(B)) == snap

# 7. delete: needs a yes, takes a safety copy, the save is gone
snap = sha(A)
lines = run(A, "m3", "m3", "c0")
assert sha(A) == snap and kind(lines, "CONFIRM|Delete this save?|")[0].endswith("|1")
lines = run(A, "m3", "m3", "c1")
assert kind(lines, "TOAST|1|Save deleted") and names[0] not in saves(A)
assert host("mass:/WLE-Safety/A-0002.mcd").read_bytes() != host(A).read_bytes()

# 8. .psu export and import round trip
lines = run(B, "m3", "m2", "k0", "pmass:/out/")
assert kind(lines, "TOAST|1|.psu written and verified"), lines
psu = host("mass:/out/BESLES-12345GAME.psu")
assert psu.exists() and psu.stat().st_size % 512 == 0
# the same save as a signed PS3 .psv, named the way a PS3 looks for it; converting the .psu gives the same bytes
PSV2 = "BESLES-12345" + b"GAME".hex().upper() + ".PSV"
lines = run(B, "m3", "m2", "k1", "pmass:/out/")
assert kind(lines, "PICK|Export .psv to|") and kind(lines, "TOAST|1|.psv written and verified"), lines
v = host("mass:/out/" + PSV2).read_bytes()
assert v[:4] == b"\0VSP" and v[0x3C] == 2 and v[0x80:0xA0].rstrip(b"\0") == b"BESLES-12345GAME"
host("mass:/conv").mkdir()
shutil.copy(psu, host("mass:/conv/BESLES-12345GAME.psu"))
lines = run("convert:mass:/conv/BESLES-12345GAME.psu", "k1", "c1")        # a .psu offers .psv first
assert kind(lines, "CHOICE|Convert to|3|1"), lines
assert kind(lines, "CONFIRM|Convert to .psv?|") and host("mass:/conv/" + PSV2).read_bytes() == v, lines
lines = run("convert:mass:/conv/BESLES-12345GAME.psu", "k1")              # never replaced, never renamed
assert kind(lines, "MESSAGE|1|Not converted|A .psv with this save's name is already there"), lines
# and as a CodeBreaker .cbs: exported from the card, converted from the .psu (same bytes, -2 when taken)
lines = run(B, "m3", "m2", "k2", "pmass:/out/")
assert kind(lines, "PICK|Export .cbs to|") and kind(lines, "TOAST|1|.cbs written and verified"), lines
c = host("mass:/out/BESLES-12345GAME.cbs").read_bytes()
assert c[:4] == b"CFU\0" and c[0x14:0x34].rstrip(b"\0") == b"BESLES-12345GAME"
lines = run("convert:mass:/conv/BESLES-12345GAME.psu", "k2", "c1")
assert kind(lines, "CONFIRM|Convert to .cbs?|") and host("mass:/conv/BESLES-12345GAME.cbs").read_bytes() == c, lines
lines = run("convert:mass:/conv/BESLES-12345GAME.psu", "k2", "c1")
assert host("mass:/conv/BESLES-12345GAME-2.cbs").read_bytes() == c, lines
J = "mass:/J.mcd"
blank(J)
lines = run(J, "m0", "pmass:/out/BESLES-12345GAME.cbs", "c1")             # it imports into the same save
assert kind(lines, "TOAST|1|Save imported and verified") and saves(J) == saves(B), lines
host("mass:/out/BESLES-12345GAME.cbs").unlink()
shutil.rmtree(host("mass:/conv"))
host("mass:/out/" + PSV2).unlink()
E = "mass:/E.mcd"
blank(E)
lines = run(E, "m0", "pmass:/out/BESLES-12345GAME.psu", "c1")
assert kind(lines, "TOAST|1|Save imported and verified"), lines
assert saves(E) == saves(B)
assert kind(lines, "PICK|Import which save?|mass:/|0|PSU|MCM/|MCM|MAX|CBS|SPS|XPS|PSV")
host("mass:/out/bad.psu").write_bytes(b"\x27\x84" + bytes(2000))
snap = sha(E)
lines = run(E, "m0", "pmass:/out/bad.psu")
assert any("not valid" in l for l in kind(lines, "MESSAGE|")) and sha(E) == snap
lines = run(B, "m3", "m2", "k0", "pmass:/out/", "c0")             # exists: replace declined
assert not kind(lines, "TOAST|")

# 8b. saves from other tools: a Blaze .mcm imports by its extension and content (the folder is named after
# the file), and "Convert to .psu" writes a .psu beside it that imports into the same save
import zlib as _zlib
_whole = saves(B)["BESLES-12345GAME"] if isinstance(saves(B), dict) else None
_files = [(b"icon.sys", bytes(range(256)) * 3 + bytes(196)), (b"DATA", bytes((i * 13) & 0xFF for i in range(5000)))]
_raw = b"".join(n.ljust(32, b"\0") + struct.pack("<L", len(d)) + d for n, d in _files)
_body = _zlib.compress(_raw, 9)
host("mass:/out/BASLUS-99999BLZ.mcm").write_bytes(struct.pack("<LL", 8192, 80 + len(_body)) + b"Blaze".ljust(72, b"\0") + _body)
F = "mass:/F.mcd"
blank(F)
lines = run(F, "m0", "pmass:/out/BASLUS-99999BLZ.mcm", "c1")
assert kind(lines, "TOAST|1|Save imported and verified"), lines
assert "BASLUS-99999BLZ" in saves(F), saves(F)
lines = run("convert:mass:/out/BASLUS-99999BLZ.mcm", "k0", "c1")
assert any(l.startswith("MESSAGE|") and "Converted" in l and l.endswith("|mass:/out/BASLUS-99999BLZ.psu") for l in lines), lines
G = "mass:/G.mcd"
blank(G)
lines = run(G, "m0", "pmass:/out/BASLUS-99999BLZ.psu", "c1")
assert kind(lines, "TOAST|1|Save imported and verified") and saves(G) == saves(F), lines
lines = run("convert:mass:/out/BASLUS-99999BLZ.mcm", "k0", "c1")       # never replaces: -2 suffix
assert host("mass:/out/BASLUS-99999BLZ-2.psu").read_bytes() == host("mass:/out/BASLUS-99999BLZ.psu").read_bytes()
lines = run("convert:mass:/out/BASLUS-99999BLZ.mcm", "k0", "c0")       # declined: nothing written
assert not host("mass:/out/BASLUS-99999BLZ-3.psu").exists()
host("mass:/out/junk.max").write_bytes(b"Ps2PowerSave" + bytes(200))
lines = run("convert:mass:/out/junk.max", "k0")
assert any(l.startswith("MESSAGE|") and "Not converted" in l for l in lines) and not host("mass:/out/junk.psu").exists(), lines

# 8c. Blaze itself keeps saves in the card's MCM folder as files with no extension named after the save
# folder (Berion): those import with their whole name (dots included), the picker admits them through the
# "MCM/" token, and "Convert to .psu" from a card asks where to write instead of writing onto the card
_blaze = struct.pack("<LL", 8192, 80 + len(_body)) + b"Blaze".ljust(72, b"\0") + _body
host("mass:/MCM").mkdir()
host("mass:/MCM/BASLUS-77777BLZ").write_bytes(_blaze)
host("mass:/MCM/BESLES-50468.SAV").write_bytes(_blaze)
host("mass:/MCM/junk").write_bytes(bytes(100))
host("mass:/MCM/BASLUS-88888BLZ.mcm").write_bytes(_blaze)
H = "mass:/H.mcd"
blank(H)
lines = run(H, "m0", "pmass:/MCM/BASLUS-77777BLZ", "c1")
assert kind(lines, "TOAST|1|Save imported and verified"), lines
lines = run(H, "m0", "pmass:/MCM/BESLES-50468.SAV", "c1")
assert kind(lines, "TOAST|1|Save imported and verified"), lines
lines = run(H, "m0", "pmass:/MCM/BASLUS-88888BLZ.mcm", "c1")             # a renamed copy still drops .mcm
assert kind(lines, "TOAST|1|Save imported and verified"), lines
assert "BASLUS-77777BLZ" in saves(H) and "BESLES-50468.SAV" in saves(H) and "BASLUS-88888BLZ" in saves(H), saves(H)
assert saves(H)["BASLUS-77777BLZ"] == saves(F)["BASLUS-99999BLZ"]
snap = sha(H)
lines = run(H, "m0", "pmass:/MCM/junk")
assert any("MCM folder" in l for l in kind(lines, "MESSAGE|")) and sha(H) == snap, lines
lines = run("convert:mass:/MCM/BASLUS-77777BLZ", "k0", "c1")           # off a card: beside the file
assert not kind(lines, "PICK|"), lines
assert any(l.startswith("MESSAGE|") and "Converted" in l and l.endswith("|mass:/MCM/BASLUS-77777BLZ.psu") for l in lines), lines
(dev / "mc0/MCM").mkdir(parents=True)
host("mc0:/MCM/BASLUS-77777BLZ").write_bytes(_blaze)
lines = run("convert:mc0:/MCM/BASLUS-77777BLZ", "k0", "pmass:/out/", "c1")  # on a card: pick a folder
assert kind(lines, "PICK|Write the .psu to|mass:/|1|"), lines
assert any("chosen folder" in l for l in kind(lines, "CONFIRM|Convert to .psu?|")), lines
assert any(l.startswith("MESSAGE|") and "Converted" in l and l.endswith("|mass:/out/BASLUS-77777BLZ.psu") for l in lines), lines
lines = run("convert:mc0:/MCM/BASLUS-77777BLZ", "k0")                  # pick declined: nothing written
assert not kind(lines, "CONFIRM|") and not host("mass:/out/BASLUS-77777BLZ-2.psu").exists()
assert not host("mc0:/MCM/BASLUS-77777BLZ.psu").exists()
I = "mass:/I.mcd"
blank(I)
lines = run(I, "m0", "pmass:/out/BASLUS-77777BLZ.psu", "c1")
assert kind(lines, "TOAST|1|Save imported and verified") and saves(I)["BASLUS-77777BLZ"] == saves(H)["BASLUS-77777BLZ"], lines

# 9. safety copies are pruned to the newest ten, and only ours
host("mass:/WLE-Safety/E-notes.txt").write_text("keep me")
host("mass:/WLE-Safety/Other-0001.mcd").write_bytes(b"keep")
for i in range(12):
    run(E, "m3", "m3", "c1")
    run(E, "m0", "pmass:/out/BESLES-12345GAME.psu", "c1")
kept = sorted(p.name for p in (dev / "mass/WLE-Safety").glob("E-*.mcd"))
assert kept == [f"E-{n:04d}.mcd" for n in range(16, 26)], kept
assert (dev / "mass/WLE-Safety/E-notes.txt").exists() and (dev / "mass/WLE-Safety/Other-0001.mcd").exists()

# 10. a write that fails part-way is reported; the card still reads, other saves intact
F = "mass:/F.mcd"
put(F, std)
before = saves(F)
for k in (1, 2):                                                  # delete: 1 safety + 2 card writes
    put(F, std)
    lines = run(F, "m4", "m3", "c1", env={"FAIL_WRITE_AT": str(k)})
    assert any(l.startswith("MESSAGE|1|Not done|") for l in lines), (k, lines)
    after = saves(F)
    assert {n: v for n, v in after.items() if n != names[1]} == {n: v for n, v in before.items() if n != names[1]}
G = "mass:/G.mcd"
for k in range(1, 9):                                              # copy: 1 safety + the card writes
    blank(G)
    lines = run(A, "m3", "m0", "p" + G, "c1", env={"FAIL_WRITE_AT": str(k)})
    assert any(l.startswith("MESSAGE|1|Not done|") and "|Safety copy: mass:/WLE-Safety/G-" in l
               for l in lines), (k, lines)
    assert saves(G) in ({}, {names[1]: saves(A)[names[1]]}), k     # absent or complete, never half

# 11. PCSX2 .ps2 cards: view and export only
P = "mass:/P.ps2"
put(P, fx.standard_card(8, True)[0])
lines = run(P, "m3")
menu = kind(lines, "MENU|P.ps2|")[0].split("|")
assert menu[3].startswith("!Import save...") and menu[4].startswith("Copy to a new card")
sub = kind(lines, "MENU|T:Game Slot 1|")[0].split("|")
assert sub[3].startswith("Copy to") and sub[4].startswith("!Move") and sub[5].startswith("Export") \
    and sub[6].startswith("!Delete")
lines = run(P, "m3", "m0", "pmass:/out/x.mcd")                    # copying out of it still works
blank("mass:/out/x.mcd")
lines = run(P, "m3", "m0", "pmass:/out/x.mcd", "c1")
assert kind(lines, "TOAST|1|Save copied"), lines


# 12. which card is served, and the guarded switch (simulated sd2psx)
PS2 = "mmce0:/MemoryCards/PS2/"
cards = {}
for folder, name, tag in (("Card1", "Card1-1.mcd", 1), ("Card2", "Card2-1.mcd", 2), ("Card2", "Card2-2.mcd", 3),
                          ("BOOT", "BootCard-1.mcd", 4), ("Card3", "Card3-1.mcd", 5)):
    (dev / "mmce0/MemoryCards/PS2" / folder).mkdir(parents=True, exist_ok=True)
    c = fx.Card(8, False)
    c.add_save(f"TAG{tag}", {"t": bytes([tag]) * 100})
    path = f"{PS2}{folder}/{name}"
    put(path, c.finish())
    cards[tag] = path
host("mmce0:/MemoryCards/PS2/Card3/notes.txt").write_text("not a card")


def dev_run(arg, *script, active="auto", **env):
    return run(arg, *script, active=active, env={k: str(v) for k, v in env.items()})


def served(path, **env):
    return int(kind(dev_run("served:" + path, **env), "SERVED|")[0].split("|")[1])


assert served(cards[1], CARD=1) == 2                               # reported as selected
assert served(cards[2], CARD=1) == 0                               # confirmed different
assert served(cards[1], CARD=1, REPORT_CARD=2) == 1                # stale number: content still says yes
assert served(cards[2], CARD=1, REPORT_CARD=2) == 2                # stale number: refused, to be safe
assert served(cards[2], active="mmce0:/MemoryCards/PS2/none.mcd", CARD=1) == 3
assert served("mass:/A.mcd", CARD=1) == 0


def active_card(**env):
    p = kind(dev_run("active", **env), "ACTIVE|")[0].split("|")
    return int(p[1]), int(p[4]), int(p[5]), p[6]


assert active_card(CARD=2, CHANNEL=2) == (0, 1, 1, cards[3])       # by number, one fingerprint
assert active_card(CARD=2, CHANNEL=2, REPORT_CARD=1) == (0, 1, 0, cards[3])   # stale: found by content
assert active_card(CARD=0, active=cards[4]) == (0, 1, 0, cards[4])            # BootCard reports 0
put(cards[5], fx.Card(8, False).finish())
shutil.copy(host(cards[5]), dev / "mmce0/MemoryCards/PS2/Card3/Card3-2.mcd")
r, m, _, _ = active_card(CARD=0, active=cards[5])
assert (r, m) == (-3, 2)                                           # two identical cards: not confirmed


def switch(path, **env):
    lines = dev_run("switch:" + path, **env)
    return int(kind(lines, "SWITCH|")[0].split("|")[1]), kind(lines, "SET_")


assert switch(cards[3], CARD=1) == (0, ["SET_CARD|00000002", "SET_CHANNEL|00000002"])
assert switch(cards[2], CARD=1) == (0, ["SET_CARD|00000002"])
assert switch(cards[2], CARD=1, SWITCH_BUSY=40)[0] == -2           # still busy after 15 polls
assert switch(cards[2], CARD=1, BUSY=40) == (-2, [])               # busy before: nothing sent
assert switch(cards[2], CARD=1, STALE=1)[0] == -3                  # number never agrees
assert switch(cards[2], CARD=1, active=cards[1])[0] == -3          # device kept serving Card1
assert switch(cards[4], CARD=1) == (-4, [])                        # BootCard is not a numbered card
# the firmware keeps one pending command: SET_CHANNEL waits until GET_CARD
# shows the SET_CARD was taken, so nothing is replaced
lines = dev_run("switch:" + cards[3], CARD=1, PENDING=3)
assert kind(lines, "SWITCH|0") and kind(lines, "SET_") == ["SET_CARD|00000002", "SET_CHANNEL|00000002"], lines
assert not kind(lines, "LOST|"), lines
# R3Z3N: MMCEMAN cannot leave a BootCard / Game ID card (reported as card 0)
# for a card number, so nothing is sent; nor from a named card, whose list
# position only looks like a card number (content is not CardN/CardN-C.mcd)
assert switch(cards[2], CARD=0, active=cards[4]) == (-6, [])
assert switch(cards[3], CARD=2, CHANNEL=1, active=cards[1]) == (-6, [])
# channel change on the selected card: no SET_CARD (it would reset the channel)
assert switch(cards[3], CARD=2, CHANNEL=1) == (0, ["SET_CHANNEL|00000002"])
host(f"{PS2}Card3/Card3-3.mcd").write_bytes(b"x" * 4096)
assert switch(f"{PS2}Card3/Card3-3.mcd", CARD=1) == (-5, [])
assert switch("mass:/A.mcd", CARD=1) == (-4, [])

# the card tool refuses the card the device reports as selected
snap = sha(cards[1])
lines = dev_run(cards[1], "m3", "m3", CARD=1)
assert any("reports this card as the selected one" in l for l in kind(lines, "MESSAGE|")) and sha(cards[1]) == snap


# a content-equal clone of the served card: writes stay refused (cannot be told
# apart), but switching to it is allowed and confirmed by number and channel
(dev / "mmce0/MemoryCards/PS2/Card4").mkdir(parents=True, exist_ok=True)
clone = f"{PS2}Card4/Card4-1.mcd"
shutil.copy(host(cards[1]), host(clone))
assert served(clone, CARD=1) == 1
assert int(kind(dev_run("isactive:" + cards[1], CARD=1), "ISACTIVE|")[0].split("|")[1]) == 1
assert int(kind(dev_run("isactive:" + clone, CARD=1), "ISACTIVE|")[0].split("|")[1]) == 0
assert int(kind(dev_run("isactive:" + cards[1], CARD=1, STALE=1, REPORT_CARD=2), "ISACTIVE|")[0].split("|")[1]) == 0
lines = dev_run("pick-switch:" + clone, "c1", CARD=1)
assert kind(lines, "SET_CARD|00000004") and kind(lines, "TOAST|1|Card switched and confirmed"), lines
lines = dev_run("pick-switch:" + cards[1], CARD=1)
assert kind(lines, "TOAST|3|This card is already in use") and not kind(lines, "SET_"), lines
lines = dev_run(clone, "m3", "m3", CARD=1)
assert any("exactly the same contents" in l for l in kind(lines, "MESSAGE|")), lines

# a mounted VMC blocks every write (the driver would write its cached tables back)
snap = sha(B)
lines = run(A, "m4", "m0", "p" + B, env={"VMC": "1"})
assert any("Unmount the VMC first" in l for l in kind(lines, "MESSAGE|")) and sha(B) == snap
assert not kind(lines, "SAFETY|")
lines = run(B, "m0", env={"VMC": "1"})
assert any("Unmount the VMC first" in l for l in kind(lines, "MESSAGE|")) and sha(B) == snap

# more saves than the menu lists (250): not "damaged"; duplicates still found by name
M = "mass:/M.mcd"
many = fx.Card(8, False)
for i in range(260):
    many.add_save(f"MANY{i:04d}", {"d": bytes([i & 0xFF]) * 16})
put(M, many.finish())
lines = run(M)
assert "260 saves, " in kind(lines, "MENU|M.mcd|")[0] and "(list shortened)" in kind(lines, "MENU|M.mcd|")[0]
lines = run(A, "m4", "m0", "p" + M, "c1")
assert kind(lines, "TOAST|1|Save copied and verified"), lines
got = saves(M)
assert len(got) == 261 and names[2] in got, (len(got), sorted(set(got) - {f"MANY{i:04d}" for i in range(260)}))
dup = fx.Card(8, False)
dup.add_save("MANY0255", {"d": b"z"})
put("mass:/dup.mcd", dup.finish())
snap = sha(M)
lines = run("mass:/dup.mcd", "m3", "m0", "p" + M, "c1")
assert any("already on that card" in l for l in kind(lines, "MESSAGE|")) and sha(M) == snap, lines
lines = run(M, "m1")
assert any("more saves than this tool can copy" in l for l in kind(lines, "MESSAGE|")), lines
assert not list((dev / "mass").glob("M-*MB*.mcd"))


# 13. copy the whole card to a new size: the source is only read, the result holds every save
R = "mass:/R.mcd"
put(R, std)
r_before = sha(R)
lines = run(R, "m1", "k4", "c1")
assert kind(lines, "CHOICE|New card size|7|3|1 MB")        # seven sizes, 1 MB up; default = the source's own size
assert kind(lines, "MESSAGE|4|New card made|"), lines
new = "mass:/R-16MB.mcd"
assert host(new).stat().st_size == 16 << 20 and saves(new) == saves(R) and sha(R) == r_before
lines = run(R, "m1", "k4", "c1")                                   # never overwrites: -2 suffix
assert host("mass:/R-16MB-2.mcd").exists() and kind(lines, "MESSAGE|4|New card made|")
lines = run(P, "m1", "k6", "c1")                                   # from a PCSX2 .ps2 card
assert saves("mass:/P-64MB.mcd") == saves(R)
lines = dev_run(cards[1], "m1", "k3", "c1", CARD=1)                # served card: only read; result outside MemoryCards
assert any(l.startswith("MESSAGE|4|New card made|") and l.endswith("|mmce0:/WLE-Resized/Card1-1-8MB.mcd")
           for l in lines), lines
assert saves("mmce0:/WLE-Resized/Card1-1-8MB.mcd") == saves(cards[1])
# too full for the new size: nothing left behind
big = fx.Card(16, False)
big.add_save("HUGE", {"blob": b"\xA5" * (9000 * 1024)})
put("mass:/H.mcd", big.finish())
lines = run("mass:/H.mcd", "m1", "k3", "c1")
assert any(l.startswith("MESSAGE|1|New card not made|") and "HUGE" in l for l in lines), lines
assert not host("mass:/H-8MB.mcd").exists()
for k in (1, 3, 40):                                               # a failing device mid-way: removed
    lines = run(R, "m1", "k3", "c1", env={"FAIL_WRITE_AT": str(k)})
    assert any(l.startswith("MESSAGE|1|New card not made|") for l in lines), (k, lines)
    assert not host("mass:/R-8MB.mcd").exists(), k
    assert sha(R) == r_before

# New blank card: made from nothing, only ever a new file, removed again on failure
assert '"New blank card..."' in browser and "dashCardNewBlank(fb.dir)" in browser
lines = run("blank:mass:/out", "k3", "c1")
made = host("mass:/out/Blank-8MB.mcd")
assert any(l.startswith("MESSAGE|4|Blank card made|Formatted and read back: 8134 KB free.") and
           l.endswith("|mass:/out/Blank-8MB.mcd") for l in lines), lines
blank_img = made.read_bytes()
assert len(blank_img) == 8 << 20 and saves("mass:/out/Blank-8MB.mcd") == {}
ref = fx.Card(8, False)
rootoff = ref.alloc_offset * 1024
for e in (rootoff, rootoff + 512):                                  # RTC time, raw as the PS2 stores it
    assert blank_img[e + 8:e + 16] == bytes([0, 56, 34, 12, 29, 10, 0xEA, 0x07]) == blank_img[e + 0x18:e + 0x20]
before = sha("mass:/out/Blank-8MB.mcd")
lines = run("blank:mass:/out", "k6", "c1")                          # a second one never touches the first
assert host("mass:/out/Blank-64MB.mcd").stat().st_size == 64 << 20
for k, mb in ((0, 1), (1, 2), (2, 4)):                              # the small sizes sd2psXtd offers
    lines = run("blank:mass:/out", f"k{k}", "c1")
    small = f"mass:/out/Blank-{mb}MB.mcd"
    assert host(small).stat().st_size == mb << 20 and saves(small) == {}, lines
    small_ref = fx.Card(mb, False)
    assert any(f"Formatted and read back: {small_ref.alloc_end - 1} KB free." in l
               for l in kind(lines, "MESSAGE|4|Blank card made")), (mb, lines)
lines = run(R, "m1", "k0", "c1")                                   # a copy down to 1 MB holds the std saves
assert host("mass:/R-1MB.mcd").stat().st_size == 1 << 20 and saves("mass:/R-1MB.mcd") == saves(R), lines
lines = run("blank:mass:/out/", "k3", "c1", env={"NOCLOCK": "1"})   # trailing slash, no RTC: zero times
assert any(l.endswith("|mass:/out/Blank-8MB-2.mcd") for l in kind(lines, "MESSAGE|4|Blank card made")), lines
assert host("mass:/out/Blank-8MB-2.mcd").read_bytes()[rootoff + 8:rootoff + 16] == bytes(8)
assert sha("mass:/out/Blank-8MB.mcd") == before
lines = run("blank:mass:/out", "k3")                                # declined: nothing made
assert not host("mass:/out/Blank-8MB-3.mcd").exists() and not kind(lines, "MESSAGE|")
lines = run("blank:mass:/out")                                      # size dialog backed out
assert not host("mass:/out/Blank-8MB-3.mcd").exists()
lines = run("blank:mmce0:/MemoryCards/Card1", "k4", "c1")           # MMCE: staged outside MemoryCards
assert host("mmce0:/WLE-New/Blank-16MB.mcd").stat().st_size == 16 << 20
assert any("Install as NEW selectable MMCE card" in l for l in kind(lines, "MESSAGE|4|Blank card made")), lines
assert not list(host("mmce0:/MemoryCards/Card1").glob("Blank*"))
for path in ("mc0:/APPS", "vmc0:/"):                                # cannot fit on a memory card
    lines = run("blank:" + path)
    assert any("Choose another folder" in l for l in kind(lines, "MESSAGE|1|")), lines
for k in (1, 3, 200, 255):                                          # a failing device: the partial file goes
    lines = run("blank:mass:/out", "k3", "c1", env={"FAIL_WRITE_AT": str(k)})
    assert any(l.startswith("MESSAGE|1|Blank card not made|") and "unfinished file was removed" in l
               for l in lines), (k, lines)
    assert not host("mass:/out/Blank-8MB-3.mcd").exists(), k
    assert sha("mass:/out/Blank-8MB.mcd") == before
lines = run("blank:mass:/out", "k3", "c1", env={"FAIL_WRITE_AT": "3", "FAIL_REMOVE": "1"})
assert any(l.startswith("MESSAGE|1|Blank card not made|") and "could not be removed; delete it" in l for l in lines), lines
host("mass:/out/Blank-8MB-3.mcd").unlink()

# Split into Game ID cards: one <ID>/<ID>-1.mcd per game, the source only read
S = "mass:/S.mcd"
put(S, std)
s_before = sha(S)
lines = run(S, "m2", "c1")
conf = kind(lines, "CONFIRM|Split into Game ID cards?|")[0]
assert "2 games each get a Game ID card of their own, 2 saves in all. Saves without a game serial are left out." in conf
assert any(l.startswith("MESSAGE|4|Game ID cards made|2 Game ID cards made and read back, with 2 saves. "
                        "2 saves without a game serial were left out. Copy each folder into MemoryCards/PS2/")
           and l.endswith("|mass:/S-GameID/") for l in lines), lines
whole = saves(S)
for gid, save in (("SLES-12345", "BESLES-12345GAME"), ("SLUS-99999", "BASLUS-99999")):
    card = f"mass:/S-GameID/{gid}/{gid}-1.mcd"
    assert host(card).stat().st_size == 8 << 20 and saves(card) == {save: whole[save]}, gid
    assert host(card).read_bytes()[:512] == host(S).read_bytes()[:512]       # the source card's identity
assert sha(S) == s_before
assert sorted(p.name for p in host("mass:/S-GameID").iterdir()) == ["SLES-12345", "SLUS-99999"]
made_sha = sha("mass:/S-GameID/SLUS-99999/SLUS-99999-1.mcd")
lines = run(S, "m2", "c1")                                          # again: existing cards are never replaced
assert any(l.startswith("MESSAGE|1|No new Game ID card|0 Game ID cards made and read back, with 0 saves. "
                        "2 games already had a card there; left as they were.") for l in lines), lines
assert sha("mass:/S-GameID/SLUS-99999/SLUS-99999-1.mcd") == made_sha and not kind(lines, "REMOVE|")
lines = run(S, "m2", "c0")                                          # declined: nothing written
assert not kind(lines, "MESSAGE|") and not kind(lines, "REMOVE|")
# several saves of one game share a card; system folders and other names stay out
multi = fx.Card(8, False)
multi.add_save("BASLUS-21050A", {"a": b"1" * 3000})
multi.add_save("BASLUS-21050B", {"b": b"2" * 5000})
multi.add_save("BADATA-SYSTEM", {"s": b"3"})
multi.add_save("BESLES-5000X", {"x": b"4"})                         # serial too short
multi.add_save("BISCPS-15001", {"j": b"5" * 70})
put("mass:/G.mcd", multi.finish())
lines = run("mass:/G.mcd", "m2", "c1")
assert any("2 Game ID cards made and read back, with 3 saves. 2 saves without a game serial were left out." in l
           for l in kind(lines, "MESSAGE|4|Game ID cards made|")), lines
g = saves("mass:/G.mcd")
assert saves("mass:/G-GameID/SLUS-21050/SLUS-21050-1.mcd") == {k: g[k] for k in ("BASLUS-21050A", "BASLUS-21050B")}
assert saves("mass:/G-GameID/SCPS-15001/SCPS-15001-1.mcd") == {"BISCPS-15001": g["BISCPS-15001"]}
# saves too big for 8 MB get the next size up
huge = fx.Card(16, False)
huge.add_save("BASLUS-77777", {"blob": b"\xA5" * (9000 * 1024)})
put("mass:/HG.mcd", huge.finish())
lines = run("mass:/HG.mcd", "m2", "c1")
assert kind(lines, "MESSAGE|4|Game ID cards made|1 Game ID card made and read back, with 1 save."), lines
assert host("mass:/HG-GameID/SLUS-77777/SLUS-77777-1.mcd").stat().st_size == 16 << 20
assert kind(lines, "CONFIRM|Split into Game ID cards?|1 game gets a Game ID card of its own, 1 save in all. This card")
# nothing to split: no folder is made
none = fx.Card(8, False)
none.add_save("NOICON", {"n": b"0"})
put("mass:/N.mcd", none.finish())
lines = run("mass:/N.mcd", "m2")
assert kind(lines, "MESSAGE|1|No game saves to split|") and not host("mass:/N-GameID").exists(), lines
# MMCE: staged outside MemoryCards in a folder per source card, then installed with its own action
put("mmce0:/MemoryCards/Card2/Split.mcd", std)
put("mmce0:/MemoryCards/Card2/Other.mcd", std)
lines = run("mmce0:/MemoryCards/Card2/Split.mcd", "m2", "c1")
assert any(l.startswith("MESSAGE|4|Game ID cards made|") and 'Install each with "Install as MMCE Game ID card".' in l
           and l.endswith("|mmce0:/WLE-GameID/Split/") for l in lines), lines
assert saves("mmce0:/WLE-GameID/Split/SLUS-99999/SLUS-99999-1.mcd") == {"BASLUS-99999": whole["BASLUS-99999"]}
lines = run("mmce0:/MemoryCards/Card2/Other.mcd", "m2", "c1")          # same games on another card: no collision
assert kind(lines, "MESSAGE|4|Game ID cards made|2 Game ID cards made"), lines
assert saves("mmce0:/WLE-GameID/Other/SLUS-99999/SLUS-99999-1.mcd") == {"BASLUS-99999": whole["BASLUS-99999"]}
assert not list(host("mmce0:/MemoryCards").glob("S*-*")) and not list(host("mmce0:/WLE-GameID").glob("**/*.PART"))
# built as .PART and renamed only once verified; without rename, a verified copy
shutil.rmtree(host("mass:/S-GameID"))
lines = run(S, "m2", "c1")
assert kind(lines, "RENAME|mass:/S-GameID/SLUS-99999/SLUS-99999-1.mcd.PART|mass:/S-GameID/SLUS-99999/SLUS-99999-1.mcd")
shutil.rmtree(host("mass:/S-GameID"))
lines = run(S, "m2", "c1", env={"NORENAME": "1"})
assert kind(lines, "MESSAGE|4|Game ID cards made|2 Game ID cards made"), lines
assert saves("mass:/S-GameID/SLUS-99999/SLUS-99999-1.mcd") == {"BASLUS-99999": whole["BASLUS-99999"]}
assert not list(host("mass:/S-GameID").glob("*/*.PART"))
# without rename, a copy that fails is removed; one that cannot be removed keeps its .PART beside it,
# and the next split treats that game as unfinished, never as already having a card
shutil.rmtree(host("mass:/S-GameID"))
w = int(run(S, "m2", "c1", env={"NORENAME": "1"})[-1].split("writes=")[1])
shutil.rmtree(host("mass:/S-GameID"))
lines = run(S, "m2", "c1", env={"NORENAME": "1", "FAIL_WRITE_AT": str(w - 1)})
assert any(l.startswith("MESSAGE|1|Some Game ID cards not made|1 Game ID card made") and " 1 failed (" in l
           for l in lines), lines
assert len(list(host("mass:/S-GameID").glob("*/*.mcd"))) == 1 and not list(host("mass:/S-GameID").glob("*/*.PART"))
shutil.rmtree(host("mass:/S-GameID"))
lines = run(S, "m2", "c1", env={"NORENAME": "1", "FAIL_WRITE_AT": str(w - 1), "FAIL_REMOVE": "1"})
assert any("A partly copied card could not be removed; delete that game's folder before splitting again." in l
           for l in kind(lines, "MESSAGE|1|")), lines
part = list(host("mass:/S-GameID").glob("*/*.PART"))
assert len(part) == 2 and all(Path(str(p)[:-5]).exists() for p in part), part   # remove fails for both .PARTs
lines = run(S, "m2", "c1")
assert any("2 games skipped: an unfinished .PART from an earlier split is in their folder; delete that folder" in l
           for l in kind(lines, "MESSAGE|")), lines
# an unfinished .PART from an interrupted split is never reused or taken as a card
shutil.rmtree(host("mass:/S-GameID"))
(host("mass:/S-GameID/SLUS-99999")).mkdir(parents=True)
host("mass:/S-GameID/SLUS-99999/SLUS-99999-1.mcd.PART").write_bytes(b"half")
lines = run(S, "m2", "c1")
assert any(l.startswith("MESSAGE|1|Some Game ID cards not made|1 Game ID card made") and
           "1 game skipped: an unfinished .PART from an earlier split is in its folder" in l for l in lines), lines
assert host("mass:/S-GameID/SLUS-99999/SLUS-99999-1.mcd.PART").read_bytes() == b"half"
assert not host("mass:/S-GameID/SLUS-99999/SLUS-99999-1.mcd").exists()
# a failing device: nothing is left under a card's final name, and the failure is named
for k in (1, 3, 40, 300):
    shutil.rmtree(host("mass:/S-GameID"))
    lines = run(S, "m2", "c1", env={"FAIL_WRITE_AT": str(k)})
    assert any(l.startswith("MESSAGE|1|Some Game ID cards not made|") and " failed (" in l for l in lines), (k, lines)
    assert not list(host("mass:/S-GameID").glob("*/*.PART")), k
    left = list(host("mass:/S-GameID").glob("*/*.mcd"))
    assert all(saves("mass:/" + str(p.relative_to(dev / "mass"))) for p in left), (k, left)
    assert sha(S) == s_before
shutil.rmtree(host("mass:/S-GameID"))                                   # and when the leftover cannot be removed
lines = run(S, "m2", "c1", env={"FAIL_WRITE_AT": "3", "FAIL_REMOVE": "1"})
assert any("An unfinished .PART file could not be removed; delete it before splitting again." in l
           for l in kind(lines, "MESSAGE|1|")), lines

# All saves here: every card in the folder and one level below, sorted by title, source card named
host("mass:/all/sub").mkdir(parents=True)
put("mass:/all/A.mcd", std)
one = fx.Card(8, False)
one.add_save("BASLUS-99999", {"data.bin": b"q" * 700})
put("mass:/all/sub/B.mcd", one.finish())
put("mass:/all/sub/C.ps2", fx.standard_card(8, True)[0])
host("mass:/all/junk.bin").write_bytes(b"not a card" * 100)
with open(host("mass:/all/huge.bin"), "wb") as f:                   # too big to be a card: never opened
    f.truncate(70 * 1024 * 1024)
host("mass:/all/notes.txt").write_text("x")
lines = run("all:mass:/all")
menu = kind(lines, "MENU|All saves|")
assert len(menu) == 1, lines
row = menu[0].split("|")
assert row[2] == "9 saves on 3 cards; 1 file not readable as a card", row[2]
assert row[3] == "Marked saves...~mark saves with Square first", row[3]
rows = row[4:]                                                   # menu row = list index + 1
assert len(rows) == 9, rows
titles = [r.split("~")[0] for r in rows]
# sorted by title; B's copy of BASLUS-99999 has no icon.sys but sorts under the title the other copies carry
keyed = [titles[i - 1] if titles[i] == "BASLUS-99999" else titles[i] for i in range(len(titles))]
assert keyed == sorted(keyed, key=lambda t: t.lower().encode("latin-1")), titles
details = [r.split("~")[1] for r in rows]
assert sum(d.startswith("A.mcd: ") for d in details) == 4
assert sum(d.startswith("sub/C.ps2: ") for d in details) == 4
assert [d for d in details if d.startswith("sub/B.mcd: ")] == ["sub/B.mcd: BASLUS-99999, 1 KB"]
assert "A.mcd: PS1 BISLPS-00001, 1 KB" in details
# one game's saves on different cards sit together
us = [i for i, d in enumerate(details) if "BASLUS-99999" in d]
assert us == list(range(us[0], us[0] + 3)), details
# a row opens that card's own save actions: the ECC card cannot be moved from or deleted on
c_row = next(i for i, d in enumerate(details) if d == "sub/C.ps2: NOICON, 3 KB")
lines = run("all:mass:/all", "m%d" % (c_row + 1))
save_menu = kind(lines, "MENU|NOICON|")
assert save_menu and "!Move to another card..." in save_menu[0] and "!Delete from this card..." in save_menu[0], lines
# copy one save from the list to another card: it lands verified, the list is read again
blank("mass:/all/D.mcd")
b_row = details.index("sub/B.mcd: BASLUS-99999, 1 KB")
a_before = sha("mass:/all/A.mcd")
lines = run("all:mass:/all", "m%d" % (b_row + 1), "m0", "pmass:/all/D.mcd", "c1")
assert kind(lines, "TOAST|1|Save copied and verified"), lines
assert saves("mass:/all/D.mcd") == {"BASLUS-99999": saves("mass:/all/sub/B.mcd")["BASLUS-99999"]}
assert kind(lines, "MENU|All saves|")[1].split("|")[2].startswith("10 saves on 4 cards"), lines  # not WLE-Safety
assert sha("mass:/all/A.mcd") == a_before
# one card with more than the 250 saves a single card view lists: every one is a row here
host("mass:/big").mkdir()
shutil.copy(host(M), host("mass:/big/M.mcd"))
lines = run("all:mass:/big")
row = kind(lines, "MENU|All saves|")[0].split("|")
assert row[2] == "261 saves on 1 card" and len(row[4:]) == 261, (row[2], len(row[4:]))
shutil.rmtree(host("mass:/big"))
# more than 64 folders: the card in a later one is still found
for i in range(70):
    host(f"mass:/wide/f{i:02d}").mkdir(parents=True)
shutil.copy(host("mass:/dup.mcd"), host("mass:/wide/f69/dup.mcd"))
lines = run("all:mass:/wide")
assert kind(lines, "MENU|All saves|1 save on 1 card|"), lines
shutil.rmtree(host("mass:/wide"))


# Marked saves: Square marks rows, then one copy, move or delete for all of them
def all_rows():
    return [r.split("~")[1] for r in kind(run("all:mass:/all"), "MENU|All saves|")[0].split("|")[4:]]


def rows_for(*wanted):
    d = all_rows()
    return ",".join(str(d.index(w) + 1) for w in wanted)


def safeties(folder, base):
    return sorted(p.name for p in host(folder).glob(base + "-*.mcd"))


lines = run("all:mass:/all", "m0")                                    # nothing marked yet
assert kind(lines, "TOAST|3|Mark saves with Square first"), lines
lines = run("all:mass:/all", "s0,1,2", "m-1")                         # the action row cannot be marked
m = kind(lines, "MENU|All saves|")[0].split("|")
assert not m[3].startswith("*") and m[4].startswith("*") and m[5].startswith("*"), m[3:6]
blank("mass:/all/E.mcd")
pick = rows_for("A.mcd: BESLES-12345GAME, 7 KB", "A.mcd: NOICON, 3 KB", "sub/B.mcd: BASLUS-99999, 1 KB")
a_before = sha("mass:/all/A.mcd")
lines = run("all:mass:/all", "s" + pick, "m0", "m0", "pmass:/all/E.mcd", "c1")
assert kind(lines, "MENU|3 marked saves|"), lines
assert kind(lines, "CONFIRM|Copy 3 saves?|E.mcd gets one safety copy first."), lines
assert kind(lines, "MESSAGE|4|Done|3 saves copied and verified.|Safety copy: mass:/all/WLE-Safety/E-0001.mcd"), lines
assert set(saves("mass:/all/E.mcd")) == {"BESLES-12345GAME", "NOICON", "BASLUS-99999"}
assert saves("mass:/all/E.mcd")["NOICON"] == saves("mass:/all/A.mcd")["NOICON"]
assert safeties("mass:/all/WLE-Safety", "E") == ["E-0001.mcd"]           # one safety copy for the whole batch
assert sha("mass:/all/A.mcd") == a_before
# again: every save is already there, so each is skipped and nothing is written, not even a safety copy
pick = rows_for("A.mcd: BESLES-12345GAME, 7 KB", "A.mcd: NOICON, 3 KB")
e_before = sha("mass:/all/E.mcd")
lines = run("all:mass:/all", "s" + pick, "m0", "m0", "pmass:/all/E.mcd", "c1")
assert kind(lines, "MESSAGE|1|Done|Nothing copied. 2 skipped. First: NOICON: A save with this name is already on that card."), lines
assert sha("mass:/all/E.mcd") == e_before and safeties("mass:/all/WLE-Safety", "E") == ["E-0001.mcd"]
# a save marked on the target card itself is counted, not copied
pick = rows_for("E.mcd: NOICON, 3 KB")
lines = run("all:mass:/all", "s" + pick, "m0", "m0", "pmass:/all/E.mcd", "c1")
assert kind(lines, "MESSAGE|4|Done|Nothing copied. 1 already on that card."), lines
# backing out of the action menu keeps the marks
lines = run("all:mass:/all", "s" + pick, "m0", "m-1", "m-1")
assert kind(lines, "MENU|All saves|")[1].split("|")[4:].count("*" + "NOICON~E.mcd: NOICON, 3 KB") == 1, lines
# move: from two cards to a third; each card it leaves gets one safety copy, removed only once verified there
blank("mass:/all/F.mcd")
pick = rows_for("A.mcd: NOICON, 3 KB", "sub/B.mcd: BASLUS-99999, 1 KB", "A.mcd: PS1 BISLPS-00001, 1 KB")
noicon = saves("mass:/all/A.mcd")["NOICON"]
lines = run("all:mass:/all", "s" + pick, "m0", "m1", "pmass:/all/F.mcd", "c1")
assert kind(lines, "MESSAGE|4|Done|3 saves moved and verified."), lines
assert set(saves("mass:/all/F.mcd")) == {"NOICON", "BASLUS-99999", "BISLPS-00001"} and saves("mass:/all/F.mcd")["NOICON"] == noicon
assert "NOICON" not in saves("mass:/all/A.mcd") and "BISLPS-00001" not in saves("mass:/all/A.mcd")
assert saves("mass:/all/sub/B.mcd") == {}
assert safeties("mass:/all/WLE-Safety", "A") == ["A-0001.mcd"] and safeties("mass:/all/sub/WLE-Safety", "B") == ["B-0001.mcd"]
assert safeties("mass:/all/WLE-Safety", "F") == ["F-0001.mcd"]
# a marked save on a PCSX2 .ps2 card: copy only
pick = rows_for("sub/C.ps2: NOICON, 3 KB", "F.mcd: NOICON, 3 KB")
lines = run("all:mass:/all", "s" + pick, "m0", "m-1", "m-1")
menu = kind(lines, "MENU|2 marked saves|")[0]
assert "!Move to another card..." in menu and "!Delete..." in menu, menu
# delete: one safety copy per card, the served MMCE rules apply as for one save
pick = rows_for("F.mcd: NOICON, 3 KB", "F.mcd: BASLUS-99999, 1 KB", "E.mcd: NOICON, 3 KB")
lines = run("all:mass:/all", "s" + pick, "m0", "m2", "c1")
assert kind(lines, "CONFIRM|Delete 3 saves?|"), lines
assert kind(lines, "MESSAGE|4|Done|3 saves deleted."), lines
assert set(saves("mass:/all/F.mcd")) == {"BISLPS-00001"} and "NOICON" not in saves("mass:/all/E.mcd")
assert safeties("mass:/all/WLE-Safety", "F") == ["F-0001.mcd", "F-0002.mcd"]
assert safeties("mass:/all/WLE-Safety", "E") == ["E-0001.mcd", "E-0002.mcd"]
# a write failure stops the batch; what was not tried is counted
blank("mass:/all/G.mcd")
pick = rows_for("A.mcd: BESLES-12345GAME, 7 KB", "E.mcd: BASLUS-99999, 1 KB", "F.mcd: PS1 BISLPS-00001, 1 KB")
lines = run("all:mass:/all", "s" + pick, "m0", "m0", "pmass:/all/G.mcd", "c1", env={"FAIL_WRITE_AT": "2"})
msg = kind(lines, "MESSAGE|1|Not all done|")
assert msg and " 1 failed. First: " in msg[0] and "Stopped there; 2 not tried." in msg[0], lines
# a move that fails while changing the source card names that card's safety copy, not the target's
host("mass:/mv").mkdir()
for k in range(1, 120):
    put("mass:/mv/A.mcd", std)
    blank("mass:/mv/H.mcd")
    shutil.rmtree(host("mass:/mv/WLE-Safety"), ignore_errors=True)
    d = [r.split("~")[1] for r in kind(run("all:mass:/mv"), "MENU|All saves|")[0].split("|")[4:]]
    pick = str(d.index("A.mcd: NOICON, 3 KB") + 1)
    lines = run("all:mass:/mv", "s" + pick, "m0", "m1", "pmass:/mv/H.mcd", "c1", env={"FAIL_WRITE_AT": str(k)})
    msg = kind(lines, "MESSAGE|1|Not all done|")
    if msg and "NOICON" in saves("mass:/mv/H.mcd"):
        assert "|Safety copy: mass:/mv/WLE-Safety/A-0001.mcd" in msg[0], msg
        break
else:
    raise AssertionError("no write failure landed on the source card")
shutil.rmtree(host("mass:/mv"))
# nothing to show
host("mass:/empty").mkdir()
lines = run("all:mass:/empty")
assert kind(lines, "MESSAGE|1|No saves found|No memory card image"), lines
shutil.rmtree(host("mass:/all"))

# Save library: .psu files in a folder, its subfolders as sets
L = "mass:/L.mcd"
put(L, std)
host("mass:/lib/Starter").mkdir(parents=True)
for row in range(3, 7):                                                  # export all four saves
    run(L, "m%d" % row, "m2", "k0", "pmass:/lib/")
for n in ("BASLUS-99999", "NOICON"):
    shutil.copy(host("mass:/lib/%s.psu" % n), host("mass:/lib/Starter/%s.psu" % n))
host("mass:/lib/bad.psu").write_bytes(b"\x27\x84" + bytes(2000))
host("mass:/lib/WLE-Safety").mkdir()
shutil.copy(host("mass:/lib/NOICON.psu"), host("mass:/lib/WLE-Safety/NOICON.psu"))  # never a set


def lib_rows():
    m = kind(run("lib:mass:/lib", "m-1"), "MENU|Save library|")[0].split("|")
    return m[2], [r.split("~")[1] if "~" in r else r for r in m[3:]]


sub, rows = lib_rows()
assert sub == "7 .psu files, 1 set", sub
assert rows[0] == "mark saves with Square first" and rows[1] == "1 set: the folders here with .psu files", rows[:2]
files = rows[2:]
assert sorted(files) == sorted(["BESLES-12345GAME.psu: BESLES-12345GAME, 7 KB", "BASLUS-99999.psu: BASLUS-99999, 3 KB",
                                "BISLPS-00001.psu: BISLPS-00001, 1 KB", "NOICON.psu: NOICON, 3 KB",
                                "Starter/BASLUS-99999.psu: BASLUS-99999, 3 KB", "Starter/NOICON.psu: NOICON, 3 KB",
                                "bad.psu: not a usable .psu"]), files


def lib_row(detail):
    return lib_rows()[1].index(detail)


# one file to a card: one safety copy, read back
X = "mass:/X.mcd"
blank(X)
lines = run("lib:mass:/lib", "m%d" % lib_row("BESLES-12345GAME.psu: BESLES-12345GAME, 7 KB"), "m0", "pmass:/X.mcd", "c1")
assert kind(lines, "CONFIRM|Copy 1 save?|X.mcd gets one safety copy first."), lines
assert kind(lines, "MESSAGE|4|Done|1 save copied and verified.|Safety copy: mass:/WLE-Safety/X-0001.mcd"), lines
assert saves(X)["BESLES-12345GAME"] == saves(L)["BESLES-12345GAME"]
# a file that is not a save cannot be copied, only deleted
lines = run("lib:mass:/lib", "m%d" % lib_row("bad.psu: not a usable .psu"), "m-1")
assert "!Copy to a card..." in kind(lines, "MENU|bad.psu|")[0], lines
# apply the set: both saves copied, one more safety copy of the card
lines = run("lib:mass:/lib", "m1", "m0", "pmass:/X.mcd", "c1")
assert kind(lines, "MENU|Apply which set?|") and "Starter~2 saves" in kind(lines, "MENU|Apply which set?|")[0], lines
assert kind(lines, "CONFIRM|Apply set Starter: copy 2 saves?|"), lines
assert kind(lines, "MESSAGE|4|Done|2 saves copied and verified."), lines
assert set(saves(X)) == {"BESLES-12345GAME", "BASLUS-99999", "NOICON"} and saves(X)["NOICON"] == saves(L)["NOICON"]
assert safeties("mass:/WLE-Safety", "X") == ["X-0001.mcd", "X-0002.mcd"]
# again: nothing replaced, nothing written, not even a safety copy
x_before = sha(X)
lines = run("lib:mass:/lib", "m1", "m0", "pmass:/X.mcd", "c1")
assert kind(lines, "MESSAGE|1|Done|Nothing copied. 2 skipped. First: "), lines
assert sha(X) == x_before and safeties("mass:/WLE-Safety", "X") == ["X-0001.mcd", "X-0002.mcd"]
# the served MMCE card is refused before anything is written
snap = sha(C1)
lines = run("lib:mass:/lib", "m1", "m0", "p" + C1, "m-1", active=C1)
assert any("This card is in use" in l for l in kind(lines, "MESSAGE|")) and not kind(lines, "CONFIRM|"), lines
assert sha(C1) == snap
# nothing marked
lines = run("lib:mass:/lib", "m0", "m-1")
assert kind(lines, "TOAST|3|Mark saves with Square first"), lines
# mark two and save them as a new set; the files are copied byte for byte
pick = ",".join(str(lib_row(d)) for d in ("BISLPS-00001.psu: BISLPS-00001, 1 KB", "BESLES-12345GAME.psu: BESLES-12345GAME, 7 KB"))
lines = run("lib:mass:/lib", "s" + pick, "m0", "m1", "fMy Set", "c1")
assert kind(lines, "MENU|2 marked saves|"), lines
assert kind(lines, "NAME|Name of the set|mass:/lib/"), lines
assert kind(lines, "CONFIRM|Make this set?|"), lines
assert kind(lines, "MESSAGE|4|Done|2 files copied into My Set.|mass:/lib/My Set/"), lines
for n in ("BISLPS-00001", "BESLES-12345GAME"):
    assert host("mass:/lib/My Set/%s.psu" % n).read_bytes() == host("mass:/lib/%s.psu" % n).read_bytes()
assert lib_rows()[0] == "9 .psu files, 2 sets"
# saving into it again leaves what is there
pick = ",".join(str(lib_row(d)) for d in ("BISLPS-00001.psu: BISLPS-00001, 1 KB", "BESLES-12345GAME.psu: BESLES-12345GAME, 7 KB"))
lines = run("lib:mass:/lib", "s" + pick, "m0", "m1", "fMy Set", "c1")
assert kind(lines, "CONFIRM|Add to this set?|") and kind(lines, "MESSAGE|4|Done|0 files copied into My Set. 2 already there."), lines
# names that would not be read back as a set are refused
lines = run("lib:mass:/lib", "s" + pick, "m0", "m1", "fWLE-x", "m-1")
assert kind(lines, "MESSAGE|1|Choose another name|"), lines
# a marked file that is not a save: copying is off, deleting works
bad = str(lib_row("bad.psu: not a usable .psu"))
lines = run("lib:mass:/lib", "s" + bad, "m0", "m-1", "m-1")
assert "!Copy to a card..." in kind(lines, "MENU|1 marked save|")[0], lines
lines = run("lib:mass:/lib", "s" + bad, "m0", "m2", "c1", "m-1")
assert kind(lines, "CONFIRM|Delete 1 .psu file?|") and kind(lines, "TOAST|1|.psu deleted"), lines
assert not host("mass:/lib/bad.psu").exists()
# a card write failure stops the batch and says what was not tried
blank("mass:/Y.mcd")
pick = ",".join(str(lib_row(d)) for d in ("BISLPS-00001.psu: BISLPS-00001, 1 KB", "BESLES-12345GAME.psu: BESLES-12345GAME, 7 KB", "NOICON.psu: NOICON, 3 KB"))
lines = run("lib:mass:/lib", "s" + pick, "m0", "m0", "pmass:/Y.mcd", "c1", "m-1", env={"FAIL_WRITE_AT": "2"})
msg = kind(lines, "MESSAGE|1|Not all done|")
assert msg and " 1 failed. First: " in msg[0] and "Stopped there; " in msg[0], lines
# a set the 500-file list cannot hold in full is shown but cannot be applied
host("mass:/big/Zset").mkdir(parents=True)
one = host("mass:/lib/NOICON.psu").read_bytes()
for i in range(499):
    host("mass:/big/N%03d.psu" % i).write_bytes(one)
for i in range(2):
    host("mass:/big/Zset/Z%d.psu" % i).write_bytes(one)
lines = run("lib:mass:/big", "m1", "m-1", "m-1")
assert kind(lines, "MENU|Save library|500 .psu files, 1 set (list shortened)|"), lines
assert "|!Zset~too many files here to list them all" in kind(lines, "MENU|Apply which set?|")[0], lines
shutil.rmtree(host("mass:/big"))
# nothing here
host("mass:/nolib").mkdir()
lines = run("lib:mass:/nolib")
assert kind(lines, "MESSAGE|4|No .psu files here|"), lines
shutil.rmtree(host("mass:/lib"))

# PS1 card images: Card contents opens the PS1 screen; import, export, delete
# through the real dialogs, each write behind a verified safety copy
def ps1_mcs(name, blocks, title):
    data = bytearray(random.Random(name).randbytes(blocks * 8192))
    data[0:4] = b"SC\x11" + bytes([blocks])
    data[4:68] = title.ljust(64, b"\0")
    f = bytearray(128)
    struct.pack_into("<IIH", f, 0, 0x51, len(data), 0xFFFF)
    f[10:10 + len(name)] = name.encode()
    x = 0
    for b in f[:127]:
        x ^= b
    f[127] = x
    return bytes(f) + bytes(data)


lines = run("blank:mass:/", "k7", "k0", "c1", "m-1")
assert kind(lines, "CHOICE|New card size|8|3|1 MB") and kind(lines, "CHOICE|New PS1 card|10|0|Blank-PS1.mcd"), lines
assert kind(lines, "TOAST|1|PS1 card made and read back: 15 blocks free"), lines
assert kind(lines, "MENU|Blank-PS1.mcd|PS1 card: 0 saves, 15 of 15 blocks free|Import save...~"), lines   # opens on it
P1 = "mass:/Blank-PS1.mcd"
assert host(P1).stat().st_size == 131072 and host(P1).read_bytes()[:2] == b"MC"
lines = run(P1)
assert kind(lines, "MENU|Blank-PS1.mcd|PS1 card: 0 saves, 15 of 15 blocks free|Import save...~"), lines
s1 = ps1_mcs("BASLUS-00001PS1", 2, b"PS1 game")
host("mass:/in.mcs").write_bytes(s1)
empty = host(P1).read_bytes()
lines = run(P1, "m0", "pmass:/in.mcs", "c1", "m-1")
assert kind(lines, "PICK|Import which PS1 save?|mass:/|0|"), lines
assert kind(lines, "CONFIRM|Import this save?|BASLUS-00001PS1, 2 blocks, from a PS1 single save (.mcs)."), lines
assert kind(lines, "TOAST|1|Save imported and verified"), lines
assert host("mass:/WLE-Safety/Blank-PS1-0001.mcd").read_bytes() == empty
assert "|T:PS1 game~BASLUS-00001PS1, 2 blocks" in kind(lines, "MENU|Blank-PS1.mcd|PS1 card: 1 save, 13 of 15 blocks free")[0], lines
# the same name again is refused before any safety copy
before = sha(P1)
lines = run(P1, "m0", "pmass:/in.mcs", "m-1")
msg = kind(lines, "MESSAGE|1|Not imported|A save with that name is already on this card.")
assert msg and msg[0].endswith("|BASLUS-00001PS1"), lines      # names the save, not the file
assert sha(P1) == before and not host("mass:/WLE-Safety/Blank-PS1-0002.mcd").exists()
# export writes the .mcs next to the card, byte for byte
lines = run(P1, "m2", "m0", "k0", "m-1")
assert kind(lines, "MESSAGE|4|Exported|"), lines
assert host("mass:/BASLUS-00001PS1.mcs").read_bytes() == s1 and sha(P1) == before
full = host(P1).read_bytes()
# a save name with path characters exports next to the card, never above it
host("mass:/evil.mcs").write_bytes(ps1_mcs("../a:b*EVIL", 1, b"Evil"))
assert kind(run(P1, "m0", "pmass:/evil.mcs", "c1", "m-1"), "TOAST|1|Save imported and verified")
lines = run(P1, "m3", "m0", "k0", "m-1")
assert kind(lines, "MESSAGE|4|Exported|") and "|mass:/_._a_b_EVIL.mcs" in kind(lines, "MESSAGE|4|Exported|")[0], lines
assert host("mass:/_._a_b_EVIL.mcs").read_bytes()[10:21] == b"../a:b*EVIL" and not list(dev.glob("*EVIL*"))
host("mass:/_._a_b_EVIL.mcs").unlink()
host("mass:/evil.mcs").unlink()
host(P1).write_bytes(full)
# a name that is not plain ASCII (a card's name field can hold any bytes) is
# not exported, and the dialog gives that reason, not a file-name problem
odd = bytearray(full)
odd[128 + 10 + 12] = 0x82                       # BASLUS-00001PS1 -> BASLUS-00001\x82S1
x = 0
for b in odd[128:255]:
    x ^= b
odd[255] = x
host("mass:/Odd-PS1.mcd").write_bytes(bytes(odd))
files = sorted(p.name for p in host("mass:/").iterdir())
lines = run("mass:/Odd-PS1.mcd", "m2", "m0", "k0", "m-1")
msg = kind(lines, "MESSAGE|1|Not exported|The save name must be 1 to 20 plain ASCII characters.")
assert msg and msg[0].endswith("|BASLUS-00001\x82S1"), lines
assert sorted(p.name for p in host("mass:/").iterdir()) == files and host("mass:/Odd-PS1.mcd").read_bytes() == odd
host("mass:/Odd-PS1.mcd").unlink()
# a file made at the chosen name after it was checked (another writer on a
# share) is never overwritten: the export and the safety copy fail instead
race = "mass:/BASLUS-00001PS1-2.mcs"       # the first name is taken by the export above
lines = run(P1, "m2", "m0", "k0", "m-1", env={"RACE_CREATE": race})
msg = kind(lines, "MESSAGE|1|Not exported|The file could not be written.")
assert msg and msg[0].endswith("|" + race), lines
assert host(race).read_bytes() == b"OTHER" and host(P1).read_bytes() == full
host(race).unlink()
race = "mass:/WLE-Safety/Blank-PS1-%04d.mcd" % (1 + max(int(p.stem[-4:]) for p in host("mass:/WLE-Safety").glob("Blank-PS1-*.mcd")))
lines = run(P1, "m2", "m1", "c1", "m-1", env={"RACE_CREATE": race})
assert kind(lines, "MESSAGE|1|Not done|The safety copy could not be made, so the card was not changed."), lines
assert host(race).read_bytes() == b"OTHER" and host(P1).read_bytes() == full
host(race).unlink()
# a save whose blocks do not add up (here a second save links into the block
# BASLUS-00001PS1 ends in) is listed with a warning; export and delete are off
link = bytearray(full)
f = bytearray(128)
struct.pack_into("<IIH", f, 0, 0x51, 2 * 8192, 1)   # block 3 starts a save that links to block 2
f[10:26] = b"BASLUS-00002LINK"
for b in f[:127]:
    f[127] ^= b
link[3 * 128:4 * 128] = f
host("mass:/Link-PS1.mcd").write_bytes(bytes(link))
files = sorted(p.name for p in host("mass:/").iterdir())
lines = run("mass:/Link-PS1.mcd", "m2", "m0", "m2", "m1", "m3", "m0", "m-1")
listed = kind(lines, "MENU|Link-PS1.mcd|PS1 card: 2 saves, 12 of 15 blocks free|")
assert listed and "~BASLUS-00001PS1, blocks do not add up|" in listed[0], lines
assert listed[0].endswith("|BASLUS-00002LINK~BASLUS-00002LINK, blocks do not add up"), lines
off = "|!Export...~its blocks do not add up|!Delete from this card...~its blocks do not add up"
assert len([l for l in lines if l.startswith("MENU|") and l.endswith(off)]) == 3, lines
assert not kind(lines, "CONFIRM|") and not kind(lines, "MESSAGE|") and not kind(lines, "TOAST|"), lines
assert sorted(p.name for p in host("mass:/").iterdir()) == files and host("mass:/Link-PS1.mcd").read_bytes() == link
host("mass:/Link-PS1.mcd").unlink()
# a failed card write is reported with the safety copy, which holds the old card
for at in ("2", "3"):     # 2: the first card write fails; 3: after 32 KB of it
    lines = run(P1, "m2", "m1", "c1", "m-1", env={"FAIL_WRITE_AT": at})
    msg = kind(lines, "MESSAGE|1|Not done|")
    assert msg and "Restore it from the safety copy" in msg[0] and "Safety copy: mass:/WLE-Safety/Blank-PS1-" in msg[0], lines
    made = msg[0].split("Safety copy: ")[1]
    assert host(made).read_bytes() == full
    host(P1).write_bytes(full)
# delete, the BIOS way
lines = run(P1, "m2", "m1", "c1", "m-1")
assert kind(lines, "CONFIRM|Delete this save?|BASLUS-00001PS1, 2 blocks."), lines
assert kind(lines, "TOAST|1|Save deleted") and kind(lines, "MENU|Blank-PS1.mcd|PS1 card: 0 saves, 15 of 15 blocks free"), lines
assert host(P1).read_bytes()[128] == 0xA1
# "All saves here" leaves PS1 cards to Card contents; a PS2 save picked for a PS1 card is refused
lines = run(P1, "m0", "pmass:/A.mcd", "m-1")
assert kind(lines, "MESSAGE|1|Not imported|"), lines
for p in ("mass:/in.mcs", "mass:/BASLUS-00001PS1.mcs", P1):
    host(p).unlink()
shutil.rmtree(host("mass:/WLE-Safety"), ignore_errors=True)

# The wLaunchELF_R3Z formats (layouts and signatures: test_mc_save_export.py): a POPStarter card by its
# exact name, opened to import saves; PS1 exports as PS3 .psv, .psx and raw; copies in other containers,
# which Card contents opens and writes back in the same container behind a safety copy of that container
host("mass:/in.mcs").write_bytes(s1)
lines = run("blank:mass:/", "k7", "k1", "c1", "m0", "pmass:/in.mcs", "c1", "m-1")
assert kind(lines, "TOAST|1|PS1 card made and read back") and kind(lines, "TOAST|1|Save imported and verified"), lines
S0 = "mass:/SLOT0.VMC"
assert kind(lines, "MENU|SLOT0.VMC|PS1 card: 1 save, 13 of 15 blocks free|"), lines
card = host(S0).read_bytes()
lines = run("blank:mass:/", "k7", "k1", "c1")                             # a fixed name is never suffixed
assert kind(lines, "MESSAGE|1|Not made|A file with this name is already in this folder.") and not kind(lines, "MENU|"), lines
assert host(S0).read_bytes() == card and not host("mass:/SLOT0-2.VMC").exists()
PSV = "mass:/BASLUS-00001" + b"PS1".hex().upper() + ".PSV"
for k, path, body in (("k1", PSV, None), ("k2", "mass:/BASLUS-00001PS1.psx", b"BASLUS-00001PS1".ljust(54, b"\0") + s1[128:]),
                      ("k3", "mass:/BASLUS-00001PS1", s1[128:])):
    lines = run(S0, "m2", "m0", k, "m-1")
    assert kind(lines, "CHOICE|Export as|4|0|.mcs: PS1 single save") and kind(lines, "MESSAGE|4|Exported|"), lines
    data = host(path).read_bytes()
    assert data == body if body else data[:4] == b"\0VSP" and data[0x3C] == 1 and data[0x84:] == s1[128:], path
    if k != "k2":                                                         # the PS3 and raw names are exact
        lines = run(S0, "m2", "m0", k, "m-1")
        assert kind(lines, "MESSAGE|1|Not exported|A file with this name is already next to the card."), lines
assert host(S0).read_bytes() == card
for k, ext, size in (("k1", "gme", 3904 + 131072), ("k2", "vgs", 64 + 131072), ("k3", "vmp", 128 + 131072),
                     ("k4", "mcx", 128 + 131072 + 32)):
    lines = run(S0, "m1", k, "m-1")
    assert kind(lines, "MESSAGE|4|Copy saved|") and host("mass:/SLOT0." + ext).stat().st_size == size, lines
    C = "mass:/SLOT0." + ext
    lines = run(C, "m-1")
    assert kind(lines, "MENU|SLOT0.%s|PS1 card (" % ext) and "): 1 save, 13 of 15 blocks free|" in kind(lines, "MENU|")[0], lines
    old = host(C).read_bytes()
    lines = run(C, "m2", "m1", "c1", "m0", "p" + PSV, "c1", "m-1")       # delete, then import the PS3 .psv back
    assert kind(lines, "TOAST|1|Save deleted") and kind(lines, "TOAST|1|Save imported and verified"), lines
    assert host(C).stat().st_size == size and kind(lines, "MENU|SLOT0.%s|" % ext)[-1].split("|")[2].endswith("1 save, 13 of 15 blocks free")
    # numbered across SLOT0.* (0001 is the import into SLOT0.VMC), two per card: the first is the container as it was
    assert host("mass:/WLE-Safety/SLOT0-%04d.mcd" % (2 * int(k[1:]))).read_bytes() == old
    if ext == "gme":
        new = host(C).read_bytes()
        assert new[:3904] == old[:3904] and new[3904:] == card            # same header, same card again
lines = run("mass:/SLOT0.mcx", "m1", "k0", "m-1")                         # back to a raw card from the encrypted one
assert kind(lines, "MESSAGE|4|Copy saved|") and host("mass:/SLOT0.mcd").read_bytes() == card, lines
lines = run("blank:mass:/", "k7", "k8", "c1", "m-1")                      # a new container card
assert kind(lines, "MENU|Blank-PS1.vmp|PS1 card (PSP / Vita .vmp): 0 saves, 15 of 15 blocks free|"), lines
for p in ("mass:/in.mcs", S0, PSV, "mass:/BASLUS-00001PS1.psx", "mass:/BASLUS-00001PS1", "mass:/SLOT0.mcd",
          "mass:/Blank-PS1.vmp", *("mass:/SLOT0." + e for e in ("gme", "vgs", "vmp", "mcx"))):
    host(p).unlink()
shutil.rmtree(host("mass:/WLE-Safety"), ignore_errors=True)

print("Card contents tool: listing, copy, move, delete, .psu round trip, safety copies pruned to ten, "
      "served MMCE card never written, served-card detection by number and content, guarded switch (also to a clone), "
      "mounted VMC blocks writes, 250+ save cards, copy to a new card size, Game ID split, blank cards, all saves across cards (also 250+ saves on one card, 64+ folders), marked-save batches, .psu library and sets, refusals cost no safety copy, write failures reported, "
      "PS1 cards (blank, import, export, delete, failed writes), PS3 .psv/.psx/raw exports, .gme/.vgs/.vmp/.mcx cards, POPStarter card names, one open file at a time: OK")
