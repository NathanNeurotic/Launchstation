"""Card contents tool: the real src/dash_card_tool.c driven through scripted dialogs.

Built with the real card viewer and writer and POSIX stand-ins for the PS2
file calls (tests/dash_card_tool_host.c). Checks what reaches the cards: the
save lands or goes, a verified safety copy is taken before every write and
pruned to ten, the card the MMCE device is serving is never written, refusals
cost no safety copy, failures are reported, and no two files are ever open.
"""
from pathlib import Path
import hashlib
import shutil
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
tool, lister = tmp / "tool", tmp / "list"
flags = ["gcc", "-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
         "-fno-sanitize-recover=all"]
subprocess.run(flags + [f"-I{root/'tests/card-tool-shim'}", f"-I{root/'include'}",
                        str(root / "tests/dash_card_tool_host.c"), str(root / "src/dash_card_tool.c"),
                        str(root / "src/mmce_card_switch.c"),
                        str(root / "src/mc_card_write.c"), str(root / "src/mc_card_view.c"),
                        str(root / "src/mc_sha256.c"), "-o", str(tool)], check=True)
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
assert menu[3].startswith("Import .psu...")
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
lines = run(B, "m3", "m2", "pmass:/out/")
assert kind(lines, "TOAST|1|.psu written and verified"), lines
psu = host("mass:/out/BESLES-12345GAME.psu")
assert psu.exists() and psu.stat().st_size % 512 == 0
E = "mass:/E.mcd"
blank(E)
lines = run(E, "m0", "pmass:/out/BESLES-12345GAME.psu", "c1")
assert kind(lines, "TOAST|1|Save imported and verified"), lines
assert saves(E) == saves(B)
assert kind(lines, "PICK|Import which .psu?|mass:/|0|PSU")
host("mass:/out/bad.psu").write_bytes(b"\x27\x84" + bytes(2000))
snap = sha(E)
lines = run(E, "m0", "pmass:/out/bad.psu")
assert any("not valid" in l for l in kind(lines, "MESSAGE|")) and sha(E) == snap
lines = run(B, "m3", "m2", "pmass:/out/", "c0")                   # exists: replace declined
assert not kind(lines, "TOAST|")

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
assert menu[3].startswith("!Import .psu...") and menu[4].startswith("Copy to a new card")
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
lines = run(R, "m1", "k1", "c1")
assert kind(lines, "CHOICE|New card size|4|0|8 MB")
assert kind(lines, "MESSAGE|4|New card made|"), lines
new = "mass:/R-16MB.mcd"
assert host(new).stat().st_size == 16 << 20 and saves(new) == saves(R) and sha(R) == r_before
lines = run(R, "m1", "k1", "c1")                                   # never overwrites: -2 suffix
assert host("mass:/R-16MB-2.mcd").exists() and kind(lines, "MESSAGE|4|New card made|")
lines = run(P, "m1", "k3", "c1")                                   # from a PCSX2 .ps2 card
assert saves("mass:/P-64MB.mcd") == saves(R)
lines = dev_run(cards[1], "m1", "k0", "c1", CARD=1)                # served card: only read; result outside MemoryCards
assert any(l.startswith("MESSAGE|4|New card made|") and l.endswith("|mmce0:/WLE-Resized/Card1-1-8MB.mcd")
           for l in lines), lines
assert saves("mmce0:/WLE-Resized/Card1-1-8MB.mcd") == saves(cards[1])
# too full for the new size: nothing left behind
big = fx.Card(16, False)
big.add_save("HUGE", {"blob": b"\xA5" * (9000 * 1024)})
put("mass:/H.mcd", big.finish())
lines = run("mass:/H.mcd", "m1", "k0", "c1")
assert any(l.startswith("MESSAGE|1|New card not made|") and "HUGE" in l for l in lines), lines
assert not host("mass:/H-8MB.mcd").exists()
for k in (1, 3, 40):                                               # a failing device mid-way: removed
    lines = run(R, "m1", "k0", "c1", env={"FAIL_WRITE_AT": str(k)})
    assert any(l.startswith("MESSAGE|1|New card not made|") for l in lines), (k, lines)
    assert not host("mass:/R-8MB.mcd").exists(), k
    assert sha(R) == r_before

# New blank card: made from nothing, only ever a new file, removed again on failure
assert '"New blank card..."' in browser and "dashCardNewBlank(fb.dir)" in browser
lines = run("blank:mass:/out", "k0", "c1")
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
lines = run("blank:mass:/out", "k3", "c1")                          # a second one never touches the first
assert host("mass:/out/Blank-64MB.mcd").stat().st_size == 64 << 20
lines = run("blank:mass:/out/", "k0", "c1", env={"NOCLOCK": "1"})   # trailing slash, no RTC: zero times
assert any(l.endswith("|mass:/out/Blank-8MB-2.mcd") for l in kind(lines, "MESSAGE|4|Blank card made")), lines
assert host("mass:/out/Blank-8MB-2.mcd").read_bytes()[rootoff + 8:rootoff + 16] == bytes(8)
assert sha("mass:/out/Blank-8MB.mcd") == before
lines = run("blank:mass:/out", "k0")                                # declined: nothing made
assert not host("mass:/out/Blank-8MB-3.mcd").exists() and not kind(lines, "MESSAGE|")
lines = run("blank:mass:/out")                                      # size dialog backed out
assert not host("mass:/out/Blank-8MB-3.mcd").exists()
lines = run("blank:mmce0:/MemoryCards/Card1", "k1", "c1")           # MMCE: staged outside MemoryCards
assert host("mmce0:/WLE-New/Blank-16MB.mcd").stat().st_size == 16 << 20
assert any("Install as NEW selectable MMCE card" in l for l in kind(lines, "MESSAGE|4|Blank card made")), lines
assert not list(host("mmce0:/MemoryCards/Card1").glob("Blank*"))
for path in ("mc0:/APPS", "vmc0:/"):                                # cannot fit on a memory card
    lines = run("blank:" + path)
    assert any("Choose another folder" in l for l in kind(lines, "MESSAGE|1|")), lines
for k in (1, 3, 200, 255):                                          # a failing device: the partial file goes
    lines = run("blank:mass:/out", "k0", "c1", env={"FAIL_WRITE_AT": str(k)})
    assert any(l.startswith("MESSAGE|1|Blank card not made|") and "unfinished file was removed" in l
               for l in lines), (k, lines)
    assert not host("mass:/out/Blank-8MB-3.mcd").exists(), k
    assert sha("mass:/out/Blank-8MB.mcd") == before

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
# MMCE: staged outside MemoryCards, then installed with its own action
put("mmce0:/MemoryCards/Card2/Split.mcd", std)
lines = run("mmce0:/MemoryCards/Card2/Split.mcd", "m2", "c1")
assert any(l.startswith("MESSAGE|4|Game ID cards made|") and 'Install each with "Install as MMCE Game ID card".' in l
           and l.endswith("|mmce0:/WLE-GameID/") for l in lines), lines
assert saves("mmce0:/WLE-GameID/SLUS-99999/SLUS-99999-1.mcd") == {"BASLUS-99999": whole["BASLUS-99999"]}
assert not list(host("mmce0:/MemoryCards").glob("S*-*"))
# a failing device: the partial card is removed and the failure named
for k in (1, 3, 40, 300):
    shutil.rmtree(host("mass:/S-GameID"))
    lines = run(S, "m2", "c1", env={"FAIL_WRITE_AT": str(k)})
    assert any(l.startswith("MESSAGE|1|Some Game ID cards not made|") and "failed and" in l for l in lines), (k, lines)
    left = list(host("mass:/S-GameID").glob("*/*.mcd"))
    assert all(saves("mass:/" + str(p.relative_to(dev / "mass"))) for p in left), (k, left)
    assert sha(S) == s_before

print("Card contents tool: listing, copy, move, delete, .psu round trip, safety copies pruned to ten, "
      "served MMCE card never written, served-card detection by number and content, guarded switch (also to a clone), "
      "mounted VMC blocks writes, 250+ save cards, copy to a new card size, Game ID split, blank cards, refusals cost no safety copy, write failures reported, "
      "one open file at a time: OK")
