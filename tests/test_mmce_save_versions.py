"""Run the real per-save version code (src/mmce_save_versions.c) on host card images.

Cards come from tests/mc_fixture.py. Each .psu written is compared byte for byte
with one made independently by tests/mc_card_write_host.c (psuout). Faults are
injected through the harness: short writes, corrupted writes and a failed
rename must leave no .psu and no .PART behind, and must not record the copy as
done, so the next run tries again.
"""
import atexit
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent))
import mc_fixture as fx

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
yaml = (root / "ps2.yaml").read_text(encoding="utf-8")
backup = (root / "src/mmce_backup.c").read_text(encoding="utf-8")
google = (root / "src/cloud_google.c").read_text(encoding="utf-8")
store = (root / "src/dash_store.c").read_text(encoding="utf-8")
settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
assert "python3 tests/test_mmce_save_versions.py" in workflow
assert "src/mmce_save_versions.c" in yaml

# Wiring: versions follow a successful card backup only, never replace it.
assert "if (backup_one(child, relative, backup_root, options, NULL, stats) == 0)\n" \
       "                save_versions(relative, backup_root, options, stats);" in backup
assert "if (!options->per_save)\n        return;" in backup
assert "MMCE_SAVE_VERSIONS_DIR" in backup
# The cloud uploads and lists them, after the same full .psu check.
assert 'genCmpFileExt(name, "PSU")' in google
assert "return mmceSaveVersionValidate(path) == 0;" in google
assert 'genCmpFileExt(e->name, "PSU")' in google
assert "int cloudGoogleRecoverSaveVersion(" in google
# Off by default and persisted.
assert "dashCfg.mmce_backup_saves = 0;" in store and '"Dash_MMCEBackupSaves = %d\\r\\n"' in store
assert "S_MMCE_BACKUP_SAVES" in settings

tmp = Path(tempfile.mkdtemp(prefix="save-versions-"))
atexit.register(shutil.rmtree, tmp, ignore_errors=True)
tool, lister = tmp / "tool", tmp / "list"
flags = ["gcc", "-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
         "-fno-sanitize-recover=all"]
subprocess.run(flags + [f"-I{root/'tests/card-tool-shim'}", f"-I{root/'include'}",
                        str(root / "tests/mmce_save_versions_host.c"), str(root / "src/mmce_save_versions.c"),
                        str(root / "src/mc_card_write.c"), str(root / "src/mc_card_view.c"),
                        str(root / "src/mc_sha256.c"), "-o", str(tool)], check=True)
subprocess.run(flags + [f"-I{root/'include'}", str(root / "tests/mc_card_write_host.c"),
                        str(root / "src/mc_card_write.c"), str(root / "src/mc_card_view.c"),
                        str(root / "src/mc_sha256.c"), "-o", str(lister)], check=True)


def call(*args, env=None):
    import os
    e = dict(os.environ, **(env or {}))
    out = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True, env=e, timeout=120)
    assert out.returncode == 0, (args, out.stdout, out.stderr[-2000:])
    lines = out.stdout.splitlines()
    assert lines[-1] == "END|open=0|max_open=1" or lines[-1] == "END|open=0|max_open=0", lines
    return lines[0].split("|")[1:]


def run(history, saves_dir, keep, env=None):
    return [int(x) for x in call("run", history, saves_dir, keep, env=env)]


def psu(image, save):
    out = tmp / "ref.psu"
    r = subprocess.run([str(lister), "psuout", str(image), save, str(out)], capture_output=True, text=True,
                       check=True)
    assert r.stdout.startswith("r=0 "), r.stdout
    return out.read_bytes()


def versions(saves_dir):
    return sorted(str(p.relative_to(saves_dir)) for p in saves_dir.rglob("*") if p.is_file())


def card(changes=0, ecc=False, extra=None):
    c = fx.Card(8, ecc)
    c.add_save("BADATA-SYSTEM", {"history": b"h" * 300})
    c.add_save("BESLES-12345GAME", {"icon.sys": fx.icon_sys(b"Game Slot 1", 4), "BESLES-12345GAME": b"x" * 3000})
    c.add_save("BASLUS-99999", {"icon.sys": fx.icon_sys(b"Two", 3), "data.bin": bytes([changes]) * 1500})
    c.add_save("NOICON", {"a": b"a" * 1024, "b": b"b" * 1025})
    for name, files in (extra or {}).items():
        c.add_save(name, files)
    return c.finish()


hist = tmp / "WLE-MMCE-Backups/mmce0/PS2/Card1/Card1-1"
hist.mkdir(parents=True)
sv = hist / "WLE-Saves"

# 1. first copy: every save but the system folder becomes a .psu, equal to an
#    independent export; the copy is recorded as done.
h1 = hist / "WLE-20261031-010000.mcd"
h1.write_bytes(card().image())
assert run(h1, sv, 3) == [0, 4, 3, 0, 1, 0]
assert versions(sv) == ["BASLUS-99999/WLE-20261031-010000.psu", "BESLES-12345GAME/WLE-20261031-010000.psu",
                        "NOICON/WLE-20261031-010000.psu", "SOURCE.TXT"]
for save in ("BASLUS-99999", "BESLES-12345GAME", "NOICON"):
    got = (sv / save / "WLE-20261031-010000.psu").read_bytes()
    assert got == psu(h1, save), save
    assert call("validate", sv / save / "WLE-20261031-010000.psu") == ["0"]
assert (sv / "SOURCE.TXT").read_text() == "WLE-20261031-010000.mcd\n"
assert h1.read_bytes() == card().image(), "the history copy is only read"

# 2. the same copy again reads nothing more
assert run(h1, sv, 3) == [0, 0, 0, 0, 0, 0]

# 3. a newer copy with the same saves keeps no new version
h2 = hist / "WLE-20261031-020000.mcd"
h2.write_bytes(card().image())
assert run(h2, sv, 3) == [0, 4, 0, 3, 1, 0]
assert len(versions(sv)) == 4 and (sv / "SOURCE.TXT").read_text() == "WLE-20261031-020000.mcd\n"

# 4. one save changed: one new version, named after the copy it came from
h3 = hist / "WLE-20261031-030000.mcd"
h3.write_bytes(card(1).image())
assert run(h3, sv, 3) == [0, 4, 1, 2, 1, 0]
assert sorted(p.name for p in (sv / "BASLUS-99999").iterdir()) == \
    ["WLE-20261031-010000.psu", "WLE-20261031-030000.psu"]
assert (sv / "BASLUS-99999/WLE-20261031-030000.psu").read_bytes() == psu(h3, "BASLUS-99999")

# 5. retention per save: keep 3 drops the oldest; the other saves keep theirs
for i in (2, 3):
    h = hist / f"WLE-20261031-0{3 + i - 1}0000.mcd"
    h.write_bytes(card(i).image())
    assert run(h, sv, 3)[2] == 1
assert sorted(p.name for p in (sv / "BASLUS-99999").iterdir()) == \
    ["WLE-20261031-030000.psu", "WLE-20261031-040000.psu", "WLE-20261031-050000.psu"]
assert len(list((sv / "NOICON").iterdir())) == 1
# keep 0 keeps every version
h = hist / "WLE-20261031-060000.mcd"
h.write_bytes(card(9).image())
assert run(h, sv, 0)[2] == 1 and len(list((sv / "BASLUS-99999").iterdir())) == 4
# keep 1 still never removes the version just written
h = hist / "WLE-20261031-070000.mcd"
h.write_bytes(card(10).image())
assert run(h, sv, 1)[2] == 1
assert [p.name for p in (sv / "BASLUS-99999").iterdir()] == ["WLE-20261031-070000.psu"]

# 6. a PCSX2 .ps2 (ECC) history copy gives the same .psu bytes
ecc_dir = tmp / "ecc"
ecc_dir.mkdir()
he = ecc_dir / "WLE-20261031-080000.ps2"
he.write_bytes(card(10, ecc=True).image())
assert run(he, ecc_dir / "WLE-Saves", 3) == [0, 4, 3, 0, 1, 0]
assert (ecc_dir / "WLE-Saves/BASLUS-99999/WLE-20261031-080000.psu").read_bytes() == \
    (sv / "BASLUS-99999/WLE-20261031-070000.psu").read_bytes()

# 7. faults: nothing half-written is left, the copy is not marked done, and
#    the next clean run writes the versions.
for fault in ("FAIL_WRITE", "CORRUPT", "NORENAME"):
    d = tmp / fault
    d.mkdir()
    hf = d / "WLE-20261031-090000.mcd"
    hf.write_bytes(card().image())
    r = run(hf, d / "WLE-Saves", 3, env={fault: "1"})
    assert r[0] == 0 and r[2] == 0 and r[5] == 3, (fault, r)
    left = versions(d / "WLE-Saves")
    assert left == [], (fault, left)
    assert run(hf, d / "WLE-Saves", 3) == [0, 4, 3, 0, 1, 0], fault

# 8. an existing file is never replaced: a second version with the same stamp
#    gets a suffix
(sv / "SOURCE.TXT").unlink()
h = hist / "WLE-20261031-070000.mcd"
h.write_bytes(card(11).image())
assert run(h, sv, 0)[2] == 1
assert sorted(p.name for p in (sv / "BASLUS-99999").iterdir()) == \
    ["WLE-20261031-070000-01.psu", "WLE-20261031-070000.psu"]

# 9. saves whose names are not plain get a folder of their own each
assert call("folder", "BASLUS-21050SAVE") == ["BASLUS-21050SAVE"]
a = call("folder", "SAVE/1")[0]
b = call("folder", "SAVE?1")[0]
assert a.startswith("SAVE_1~") and b.startswith("SAVE_1~") and a != b, (a, b)
assert call("folder", "x.")[0].startswith("x.~")
odd = tmp / "odd"
odd.mkdir()
ho = odd / "WLE-20261031-100000.mcd"
c = fx.Card(8, False)
c.add_save("A B", {"f": b"1"})
c.add_save("A_B", {"f": b"2"})
ho.write_bytes(c.finish().image())
assert run(ho, odd / "WLE-Saves", 3) == [0, 2, 2, 0, 0, 0]
folders = sorted(p.name for p in (odd / "WLE-Saves").iterdir() if p.is_dir())
assert len(folders) == 2 and "A_B" in folders and folders[1].startswith("A_B~"), folders

# 10. not a history copy, or not a card: refused, nothing written
bad = tmp / "bad"
bad.mkdir()
(bad / "card.mcd").write_bytes(card().image())
assert run(bad / "card.mcd", bad / "WLE-Saves", 3)[0] == -1
(bad / "WLE-20261031-110000.mcd").write_bytes(b"\0" * (8 * 1024 * 1024))
assert run(bad / "WLE-20261031-110000.mcd", bad / "WLE-Saves", 3)[0] == -1
assert versions(bad / "WLE-Saves") == []

# 11. .psu validation: truncated, padded or foreign files are refused
good = next((sv / "NOICON").iterdir()).read_bytes()
for name, data in (("short.psu", good[:-1500]), ("long.psu", good + b"\0" * 1024), ("junk.psu", b"PSU" * 400),
                   ("empty.psu", b"")):
    (tmp / name).write_bytes(data)
    assert call("validate", tmp / name) == ["-1"], name
(tmp / "ok.psu").write_bytes(good)
assert call("validate", tmp / "ok.psu") == ["0"]

print("PASS: per-save .psu versions: exact bytes, unchanged skipped, retention, ECC, faults, names, validation")
