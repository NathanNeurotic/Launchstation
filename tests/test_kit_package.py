"""Build LAUNCHSTATION.zip from synthetic inputs and reject every tampered or incomplete kit."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, root / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


kit = load("package_kit")
apps = load("package_apps")


def expect(exception, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except exception:
        return
    raise AssertionError(f"{function.__name__} accepted bad input")


# The committed pin is well formed and names every file the kit cannot do without.
pinned = kit.load_manifest()
assert pinned["source"]["repository"] == "NathanNeurotic/RiptOPL"
assert all(kit_path.startswith(("POPS/", "EMBER/")) for kit_path in pinned["files"])
assert pinned["files"]["EMBER/ember.elf"]["size"] > 1024 * 1024
assert "POPS/POPSTARTER.ELF" in pinned["files"] and "EMBER/games/Per-Game-Folder/GAME FILES" in pinned["files"]
for name in kit.SHORTCUT_NAMES:
    text = (kit.SHORTCUTS / name).read_text(encoding="ascii").replace("\r\n", "\n")
    assert text.startswith("[InternetShortcut]\nURL=https://"), name

with tempfile.TemporaryDirectory(prefix="kit-package-") as tmp:
    tmp = Path(tmp)
    # A stand-in RiptOPL repository holding the ship/ tree the pin draws from.
    source = tmp / "riptopl"
    payloads = {}
    for kit_path, source_path in kit.EXTERNAL_FILES:
        payloads[source_path] = b"" if kit_path.endswith("GAME FILES") else ("example:" + kit_path).encode()
    for kit_path in kit.EXTERNAL_REQUIRED:
        if kit_path.startswith("POPS/") and f"{kit.RIPTOPL_EXAMPLE}/{kit_path}" not in payloads:
            payloads["ship/" + kit_path] = ("pops:" + kit_path).encode() * 7
    payloads["ship/EMBER/ember.elf"] = b"\x7fELF" + bytes(range(256)) * 16
    payloads["ship/EMBER/LICENSE-BETA.txt"] = b"Ember beta licence\r\n"
    payloads["ship/EMBER/you must place a ps1 bios file here for ember to work"] = b""
    payloads["ship/EMBER/games/this file can be deleted"] = b""
    payloads["ship/POPS/UDPFS-NOTICES/PROVENANCE.json"] = b"{}"
    payloads["ship/POPS/UDPFS-NOTICES/LICENSE"] = b"AFL"
    payloads["ship/EMBER/Ember Official Webpage.url"] = b"[InternetShortcut]\r\nURL=https://example.invalid/\r\n"
    for path, data in payloads.items():
        (source / path).parent.mkdir(parents=True, exist_ok=True)
        (source / path).write_bytes(data)
    env = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t",
           "HOME": str(tmp), "PATH": __import__("os").environ["PATH"]}
    git = lambda *args: subprocess.run(["git", "-C", str(source), *args], check=True, capture_output=True, env=env, text=True).stdout
    git("init", "-q")
    git("add", "-A")
    git("-c", "commit.gpgsign=false", "commit", "-q", "-m", "ship")
    commit = git("rev-parse", "HEAD").strip()

    manifest_path = tmp / "pin.json"
    manifest = kit.refresh_pin("test/riptopl", commit, source, manifest_path, log=lambda *_: None)
    assert set(manifest["files"]) >= set(kit.EXTERNAL_REQUIRED)
    assert manifest["files"]["POPS/CHEATS.TXT"]["path"] == f"{kit.RIPTOPL_EXAMPLE}/POPS/CHEATS.TXT"
    assert manifest["files"]["EMBER/games/Per-Game-Folder/GAME FILES"]["size"] == 0
    assert manifest["files"]["EMBER/Ember Official Webpage.url"]["sha256"] == hashlib.sha256(
        payloads["ship/EMBER/Ember Official Webpage.url"]).hexdigest()
    expect(ValueError, kit.refresh_pin, "test/riptopl", "0" * 40, source, tmp / "unused.json", log=lambda *_: None)
    expect(ValueError, kit.refresh_pin, "test/riptopl", commit[:12], source, tmp / "unused.json", log=lambda *_: None)

    # Staging copies the committed bytes and re-checks them on every run.
    cache = tmp / "externals"
    kit.fetch_externals(manifest, cache, source, log=lambda *_: None)
    assert (cache / "POPS/POPSTARTER.ELF").read_bytes() == payloads["ship/POPS/POPSTARTER.ELF"]
    (cache / "POPS/CHEATS.TXT").write_bytes(b"edited")
    kit.fetch_externals(manifest, cache, source, log=lambda *_: None)
    assert (cache / "POPS/CHEATS.TXT").read_bytes() == payloads[f"{kit.RIPTOPL_EXAMPLE}/POPS/CHEATS.TXT"]
    forged = json.loads(manifest_path.read_text())
    forged["files"]["POPS/CHEATS.TXT"]["sha256"] = "0" * 64
    expect(ValueError, kit.fetch_externals, forged, tmp / "forged", source, log=lambda *_: None)
    assert not (tmp / "forged" / "POPS/CHEATS.TXT").exists()

    # The app packages exactly as package_apps.py writes them for a release.
    elf = bytearray(6001)
    elf[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<H", elf, 18, 8)
    elf[52:] = bytes(i % 251 for i in range(len(elf) - 52))
    (tmp / "elf").write_bytes(elf)
    apps.package(tmp / "elf", tmp / "elf", tmp / "artifacts", "a" * 40, 1791500000)

    def neutrino_tree(folder, drop=()):
        for name in kit.NEUTRINO_REQUIRED + ("modules/udpbd.irx", "config/bsd-udpbd.toml"):
            if name in drop:
                continue
            (folder / name).parent.mkdir(parents=True, exist_ok=True)
            (folder / name).write_bytes(b"\x7fELF neutrino" if name == "neutrino.elf" else ("neutrino:" + name).encode())
        return folder

    neutrino = neutrino_tree(tmp / "neutrino_1.0.0")
    with zipfile.ZipFile(tmp / "neutrino_1.0.0.zip", "w") as archive:
        for path in sorted(neutrino.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(neutrino).as_posix())

    output = kit.package(tmp / "artifacts", cache, tmp / "neutrino_1.0.0.zip", tmp / "out" / "LAUNCHSTATION.zip",
                         "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    psu = (tmp / "artifacts" / "APP_LAUNCHSTATION.psu").read_bytes()
    with zipfile.ZipFile(output) as archive:
        names = set(archive.namelist())
        assert {"APPS/APP_LAUNCHSTATION/LAUNCHSTATION.ELF", "APPS/APP_LAUNCHSTATION/title.cfg",
                "APPS/APP_LAUNCHSTATION/WLE-UPDATE-CA.PEM", "APPS/APP_LAUNCHSTATION/BGM.ADP",
                "APP_LAUNCHSTATION.psu", "POPS/POPSTARTER.ELF", "POPS/CHEATS.TXT", "EMBER/ember.elf",
                "EMBER/games/Per-Game-Folder/GAME FILES", "neutrino/neutrino.elf", "neutrino/config/bsd-smb.toml",
                "PS2-Servers.url", "KIT-README.txt", "KIT-MANIFEST.json"} <= names
        assert not any(name.startswith("APP_LAUNCHSTATION/") or "-DS" in name for name in names)
        assert archive.read("APPS/APP_LAUNCHSTATION/LAUNCHSTATION.ELF") == elf
        assert archive.read("APP_LAUNCHSTATION.psu") == psu
        assert archive.read("POPS/POPSTARTER.ELF") == payloads["ship/POPS/POPSTARTER.ELF"]
        assert archive.read("neutrino/modules/smb_fhi.irx") == b"neutrino:modules/smb_fhi.irx"
        assert archive.read("KIT-README.txt") == kit.KIT_README.read_bytes()
        built = json.loads(archive.read("KIT-MANIFEST.json"))
        assert built["build"] == 284 and built["revision"] == "a" * 40
        assert built["externals"] == {"repository": "test/riptopl", "commit": commit}
        assert built["neutrino"]["source"] == "neutrino_1.0.0.zip"
        assert built["app"]["elf_sha256"] == hashlib.sha256(elf).hexdigest()
        assert set(built["files"]) == names - {"KIT-MANIFEST.json"}
        assert all(info.date_time == (2026, 10, 8, 22, 53, 20) for info in archive.infolist()), "deterministic timestamps"
    kit.verify(output, psu, manifest)
    # A second build from the same inputs is byte-identical.
    again = kit.package(tmp / "artifacts", cache, tmp / "neutrino_1.0.0.zip", tmp / "out2" / "LAUNCHSTATION.zip",
                        "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    assert again.read_bytes() == output.read_bytes()
    # An extracted Neutrino folder is accepted the same way (what 7z leaves behind in CI).
    kit.package(tmp / "artifacts", cache, neutrino, tmp / "out3" / "LAUNCHSTATION.zip",
                "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    try:
        import py7zr
    except ImportError:
        py7zr = None
    if py7zr is not None or shutil.which("7z") or shutil.which("7za") or shutil.which("7zz"):
        if py7zr is not None:
            with py7zr.SevenZipFile(tmp / "neutrino_1.0.0.7z", "w") as archive:
                for path in sorted(neutrino.rglob("*")):
                    if path.is_file():
                        archive.write(path, path.relative_to(neutrino).as_posix())
        else:
            subprocess.run([shutil.which("7z") or shutil.which("7za") or shutil.which("7zz"), "a", "-y",
                            str(tmp / "neutrino_1.0.0.7z"), "."], cwd=neutrino, check=True, stdout=subprocess.DEVNULL)
        sevenzip = kit.package(tmp / "artifacts", cache, tmp / "neutrino_1.0.0.7z", tmp / "out4" / "LAUNCHSTATION.zip",
                               "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
        with zipfile.ZipFile(sevenzip) as archive:
            assert archive.read("neutrino/neutrino.elf") == b"\x7fELF neutrino"
            assert json.loads(archive.read("KIT-MANIFEST.json"))["neutrino"]["source"] == "neutrino_1.0.0.7z"

    # Refusals: tampered externals, an incomplete Neutrino, a smuggled BIOS, escaping archive
    # members, a wrong PSU and a doctored kit never pass.
    (cache / "EMBER/ember.elf").write_bytes(b"\x7fELF tampered")
    expect(ValueError, kit.package, tmp / "artifacts", cache, neutrino, tmp / "bad" / "k.zip",
           "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    assert not (tmp / "bad").exists() or not list((tmp / "bad").iterdir())
    kit.fetch_externals(manifest, cache, source, log=lambda *_: None)
    (cache / "POPS/TROJAN_7.BIN").unlink()
    expect(FileNotFoundError, kit.package, tmp / "artifacts", cache, neutrino, tmp / "bad" / "k.zip",
           "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    kit.fetch_externals(manifest, cache, source, log=lambda *_: None)
    expect(ValueError, kit.package, tmp / "artifacts", cache, neutrino_tree(tmp / "no_smb", drop=("modules/smb_fhi.irx",)),
           tmp / "bad" / "k.zip", "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    expect(ValueError, kit.package, tmp / "artifacts", cache, neutrino_tree(tmp / "no_core", drop=("neutrino.elf",)),
           tmp / "bad" / "k.zip", "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    with zipfile.ZipFile(tmp / "escape.zip", "w") as archive:
        for name in kit.NEUTRINO_REQUIRED:
            archive.writestr(name, b"\x7fELF x")
        archive.writestr("../escape.txt", b"x")
    expect(ValueError, kit.package, tmp / "artifacts", cache, tmp / "escape.zip", tmp / "bad" / "k.zip",
           "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    with zipfile.ZipFile(tmp / "absolute.zip", "w") as archive:
        for name in kit.NEUTRINO_REQUIRED:
            archive.writestr(name, b"\x7fELF x")
        archive.writestr("/etc/passwd", b"x")
    expect(ValueError, kit.package, tmp / "artifacts", cache, tmp / "absolute.zip", tmp / "bad" / "k.zip",
           "a" * 40, 1791500000, "284", manifest_path, log=lambda *_: None)
    bios_manifest = json.loads(manifest_path.read_text())
    bios_manifest["files"]["EMBER/bios.bin"] = {"path": "ship/EMBER/bios.bin", "sha256": hashlib.sha256(b"B" * 64).hexdigest(), "size": 64}
    (tmp / "bios.json").write_text(json.dumps(bios_manifest))
    (cache / "EMBER/bios.bin").write_bytes(b"B" * 64)
    expect(ValueError, kit.package, tmp / "artifacts", cache, neutrino, tmp / "bad" / "k.zip",
           "a" * 40, 1791500000, "284", tmp / "bios.json", log=lambda *_: None)
    (cache / "EMBER/bios.bin").unlink()
    expect(ValueError, kit.package, tmp / "artifacts", cache, neutrino, tmp / "bad" / "k.zip",
           "a" * 40, 1791500000, "x", manifest_path, log=lambda *_: None)
    expect(ValueError, kit.verify, output, (tmp / "artifacts" / "APP_LAUNCHSTATION-DS.psu").read_bytes(), manifest)
    expect(ValueError, kit.verify, output, psu, forged)

    def doctored(edit):
        target = tmp / "doctored.zip"
        with zipfile.ZipFile(output) as src, zipfile.ZipFile(target, "w") as dst:
            for info in src.infolist():
                name, data = edit(info.filename, src.read(info))
                if name is not None:
                    dst.writestr(name, data)
        return target

    expect(ValueError, kit.verify, doctored(lambda n, d: (n, d + b"x" if n == "POPS/CHEATS.TXT" else d)), psu, manifest)
    expect(ValueError, kit.verify, doctored(lambda n, d: (n, b"" if n == "neutrino/config/system.toml" else d)), psu, manifest)
    expect(ValueError, kit.verify, doctored(lambda n, d: (None if n == "PS2-Servers.url" else n, d)), psu, manifest)
    expect(ValueError, kit.verify, doctored(lambda n, d: ("EMBER/bios.bin" if n == "EMBER/LICENSE-BETA.txt" else n, d)), psu, manifest)
    expect(ValueError, kit.verify, doctored(lambda n, d: (n, d.replace(b"284", b"285") if n == "KIT-MANIFEST.json" else d)), psu, manifest)
    expect(ValueError, kit.verify, doctored(lambda n, d: (n.replace("APPS/", "") if n.startswith("APPS/") else n, d)), psu, manifest)
    expect(ValueError, kit.verify, doctored(lambda n, d: (n, d + b"\n" if n == "KIT-README.txt" else d)), psu, manifest)
    # The CLI reports a refusal as a non-zero exit, never a traceback.
    result = subprocess.run([sys.executable, str(root / "scripts/package_kit.py"), "--manifest", str(manifest_path),
                             "verify", "--kit", str(tmp / "doctored.zip"), "--psu", str(tmp / "artifacts/APP_LAUNCHSTATION.psu")],
                            capture_output=True, text=True)
    assert result.returncode == 1 and result.stderr.startswith("error: ") and "Traceback" not in result.stderr
    result = subprocess.run([sys.executable, str(root / "scripts/package_kit.py"), "--manifest", str(manifest_path),
                             "verify", "--kit", str(output), "--psu", str(tmp / "artifacts/APP_LAUNCHSTATION.psu")],
                            capture_output=True, text=True, check=True)
    assert result.stdout.startswith("Verified ")
print("Kit packaging, pin staging and every refusal passed")
