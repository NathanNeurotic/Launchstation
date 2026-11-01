#!/usr/bin/env python3
"""Assemble and verify LAUNCHSTATION.zip, the full install kit.

The kit mirrors the layout of RiptOPL's release ZIP: the installable app under
APPS/APP_LAUNCHSTATION/ with its memory-card PSU beside it, POPS/ (POPStarter and
its companions), EMBER/ (the Ember PS1 core), neutrino/ (the latest upstream
Neutrino build) and the PC-side .url shortcuts. Launchstation launches VCDs
through <device>/POPS/POPSTARTER.ELF, Ember from an Ember directory holding
ember.elf and games/, and PS2 ISOs through <device>/neutrino/neutrino.elf, so the
kit is complete when copied to the root of a game device.

POPS/ and EMBER/ are not stored in this repository. ship/kit-externals.json pins
them to one commit of NathanNeurotic/RiptOPL, with the SHA-256 of every file;
`fetch` downloads (or copies from a local RiptOPL checkout) and refuses any byte
that differs from the pin, and `refresh-pin` re-derives the manifest for a newer
RiptOPL commit. Neutrino comes from the ps2max32/neutrino "latest" release .7z,
exactly as RiptOPL bundles it. Nothing here is embedded in the ELF; the ordinary
APP_LAUNCHSTATION packages and the updater's PSUs are unchanged.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "ship" / "kit-externals.json"
SHORTCUTS = ROOT / "ship" / "shortcuts"
KIT_README = ROOT / "ship" / "KIT-README.txt"
KIT_NAME = "LAUNCHSTATION.zip"
APP_FOLDER = "APP_LAUNCHSTATION"
ELF_NAME = "LAUNCHSTATION.ELF"
APP_MEMBERS = (ELF_NAME, "title.cfg", "WLE-UPDATE-CA.PEM", "BGM.ADP")
RAW_URL = "https://raw.githubusercontent.com/{repository}/{commit}/{path}"
TREES_URL = "https://api.github.com/repos/{repository}/git/trees/{commit}?recursive=1"
USER_AGENT = "Launchstation-kit-packager"

# Where each kit folder comes from inside the RiptOPL tree (the same sources its
# tools/package_edition.py copies). Whole folders are taken recursively; the
# Example ZIP folder supplies the POPStarter companions and Ember's game layout
# that live only there.
RIPTOPL_EXAMPLE = "ship/Example/APP_RIPTOPL-OFFICIALROLLING-VERSION.ZIP"
EXTERNAL_FOLDERS = (("POPS", "ship/POPS"), ("EMBER", "ship/EMBER"))
EXTERNAL_FILES = tuple((f"POPS/{name}", f"{RIPTOPL_EXAMPLE}/POPS/{name}") for name in (
    "CHEATS.TXT", "IGR_BG.TM2", "IGR_NO.TM2", "IGR_YES.TM2", "POPSTARTER.KELF", "TROJAN_7.BIN")) + (
    ("EMBER/games/Per-Game-Folder/GAME FILES", f"{RIPTOPL_EXAMPLE}/EMBER/games/Per-Game-Folder/GAME FILES"),)
# Placeholders RiptOPL ships empty on purpose (folder markers and reminders).
EMPTY_ALLOWED = frozenset((
    "EMBER/games/Per-Game-Folder/GAME FILES",
    "EMBER/games/this file can be deleted",
    "EMBER/you must place a ps1 bios file here for ember to work",
))
# Externals the kit cannot do without (every pinned file is checked; these also
# guard the pin itself against a RiptOPL reorganisation).
EXTERNAL_REQUIRED = (
    "POPS/POPSTARTER.ELF", "POPS/POPSTARTER.KELF", "POPS/CHEATS.TXT", "POPS/TROJAN_7.BIN",
    "POPS/IGR_BG.TM2", "POPS/IGR_NO.TM2", "POPS/IGR_YES.TM2", "POPS/IPCONFIG.DAT", "POPS/UDPFSCONFIG.DAT",
    "POPS/UDPFS-NOTICES/PROVENANCE.json", "POPS/UDPFS-NOTICES/LICENSE",
    "EMBER/ember.elf", "EMBER/LICENSE-BETA.txt", "EMBER/games/Per-Game-Folder/GAME FILES",
) + tuple(f"POPS/{module}.{suffix}" for suffix in ("usbexfat", "mx4sio", "mmce", "ata", "ilink", "udpfs")
          for module in ("usbd.irx", "usbhdfsd.irx")) + tuple(
    f"POPS/POPSTARTER VERSIONS/{version}/POPSTARTER.ELF"
    for version in ("MAIN", "DEBUG", "USBDELAY", "USBDELAY_DEBUG", "USBDELAY_LONGER_DEBUG"))
# What Launchstation needs from the Neutrino install: neutrino.elf beside
# config/system.toml marks a complete install (src/neutrino_iso.c), and the bsd
# configs are the backends it hands -bsd= values for. The SMB module pair plus
# its config are what #248's SMB launches require; upstream's "latest" build
# does not always carry them (v1.8.0-61 did not), so they are recorded in
# KIT-MANIFEST.json rather than required.
NEUTRINO_REQUIRED = (
    "neutrino.elf", "config/system.toml",
    "config/bsd-usb.toml", "config/bsd-mmce.toml", "config/bsd-ilink.toml", "config/bsd-udpfs.toml",
    "modules/usbmass_bd_mini.irx", "modules/mmceman.irx", "modules/IEEE1394_bd_mini.irx", "modules/udpfs_bd.irx",
)
NEUTRINO_SMB = ("config/bsd-smb.toml", "modules/smb_ioman.irx", "modules/smb_fhi.irx")
SHORTCUT_NAMES = ("PS2-Servers.url", "android-udpfs-server.url", "OPL-PS1-AIO-Converter-GUI.url",
                  "OrbitPS2-Manager.url", "Launchstation-Documentation.url")


def _packages():
    spec = importlib.util.spec_from_file_location("package_apps", ROOT / "scripts" / "package_apps.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def safe_relative(name):
    """A ZIP/archive member path that stays inside its folder, as POSIX."""
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or not path.parts or any(part in ("", ".", "..") for part in path.parts) \
            or ":" in name or any(ord(c) < 32 for c in name):
        raise ValueError(f"unsafe archive member path: {name!r}")
    return path.as_posix()


def load_manifest(path=MANIFEST):
    manifest = json.loads(Path(path).read_text(encoding="utf-8"))
    source = manifest["source"]
    commit = source["commit"]
    if len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
        raise ValueError("the externals pin must name a full lowercase commit hash")
    if not source["repository"].count("/") == 1:
        raise ValueError("the externals pin must name an owner/repository")
    files = manifest["files"]
    if not files:
        raise ValueError("the externals pin lists no files")
    for kit_path, entry in files.items():
        if safe_relative(kit_path) != kit_path or safe_relative(entry["path"]) != entry["path"]:
            raise ValueError(f"unsafe path in the externals pin: {kit_path}")
        if len(entry["sha256"]) != 64:
            raise ValueError(f"missing digest in the externals pin: {kit_path}")
    missing = sorted(set(EXTERNAL_REQUIRED) - set(files))
    if missing:
        raise ValueError(f"the externals pin lacks required kit files: {missing}")
    return manifest


def download(url, attempts=5, timeout=60):
    """Fetch a URL, retrying transient failures (5xx, 429, network errors) with backoff."""
    delay = 2
    for attempt in range(1, attempts + 1):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if error.code < 500 and error.code not in (408, 425, 429) or attempt == attempts:
                raise
        except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
            if attempt == attempts:
                raise
        time.sleep(delay)
        delay = min(delay * 2, 30)
    raise RuntimeError("unreachable")


def raw_url(repository, commit, path):
    return RAW_URL.format(repository=repository, commit=commit, path=urllib.parse.quote(path))


def git_blob(repository_dir, commit, path):
    """The committed bytes of `path` at `commit`: what GitHub serves, independent of the
    checkout's line-ending conversion (RiptOPL's .gitattributes has `* text=auto`)."""
    result = subprocess.run(["git", "-C", str(repository_dir), "show", f"{commit}:{path}"], capture_output=True)
    if result.returncode != 0:
        raise FileNotFoundError(f"pinned external missing from {repository_dir} at {commit[:12]}: {path}")
    return result.stdout


def fetch_externals(manifest, cache, source_dir=None, log=print):
    """Make every pinned external available under `cache`, byte-checked against the pin.

    Files already in the cache with the pinned digest are kept. Otherwise they are
    copied from `source_dir` (a RiptOPL checkout at the pinned commit) or
    downloaded from GitHub at the pinned commit. A file that differs from its pin
    is never written.
    """
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    repository, commit = manifest["source"]["repository"], manifest["source"]["commit"]
    fetched = reused = 0
    for kit_path, entry in sorted(manifest["files"].items()):
        target = cache / kit_path
        if target.is_file() and sha256(target.read_bytes()) == entry["sha256"]:
            reused += 1
            continue
        if source_dir is not None:
            where = f"{source_dir}:{commit[:12]}:{entry['path']}"
            data = git_blob(source_dir, commit, entry["path"])
        else:
            where = raw_url(repository, commit, entry["path"])
            data = download(where)
        if sha256(data) != entry["sha256"]:
            raise ValueError(f"external differs from its pin ({kit_path}); refusing to use {where}")
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_name(target.name + ".part")
        partial.write_bytes(data)
        partial.replace(target)
        fetched += 1
    log(f"Externals ready in {cache}: {fetched} fetched, {reused} already pinned")
    return cache


def _list_source_files(repository, commit, source_dir):
    """All repository paths at the pinned commit, from a checkout or the GitHub trees API."""
    if source_dir is not None:
        listing = subprocess.run(["git", "-C", str(source_dir), "ls-tree", "-r", "--name-only", "-z", commit],
                                 capture_output=True, check=True).stdout
        return sorted(name.decode("utf-8") for name in listing.split(b"\0") if name)
    tree = json.loads(download(TREES_URL.format(repository=repository, commit=commit)))
    if tree.get("truncated"):
        raise ValueError("the GitHub tree listing was truncated; refresh from a local checkout")
    return sorted(item["path"] for item in tree["tree"] if item["type"] == "blob")


def refresh_pin(repository, commit, source_dir=None, output=MANIFEST, log=print):
    """Re-derive ship/kit-externals.json for `commit` of `repository`."""
    if len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
        raise ValueError("--commit must be the full lowercase commit hash")
    if source_dir is not None:
        present = subprocess.run(["git", "-C", str(source_dir), "cat-file", "-e", commit + "^{commit}"],
                                 capture_output=True)
        if present.returncode != 0:
            raise ValueError(f"{source_dir} does not hold commit {commit}; fetch it first")
    available = _list_source_files(repository, commit, source_dir)
    selected = {}
    for kit_folder, source_folder in EXTERNAL_FOLDERS:
        members = [path for path in available if path.startswith(source_folder + "/")]
        if not members:
            raise ValueError(f"no files under {source_folder} at {commit}")
        for path in members:
            selected[kit_folder + path[len(source_folder):]] = path
    for kit_path, source_path in EXTERNAL_FILES:
        if source_path not in available:
            raise ValueError(f"{source_path} is missing at {commit}")
        if kit_path in selected:
            raise ValueError(f"{kit_path} is supplied twice by the RiptOPL tree")
        selected[kit_path] = source_path
    files = {}
    for kit_path in sorted(selected):
        source_path = selected[kit_path]
        data = git_blob(source_dir, commit, source_path) if source_dir is not None \
            else download(raw_url(repository, commit, source_path))
        files[kit_path] = {"path": source_path, "sha256": sha256(data), "size": len(data)}
    manifest = {
        "comment": "Kit externals pinned to one RiptOPL commit; regenerate with scripts/package_kit.py refresh-pin.",
        "source": {"repository": repository, "commit": commit,
                   "tree": f"https://github.com/{repository}/tree/{commit}"},
        "files": files,
    }
    text = json.dumps(manifest, indent=2) + "\n"
    load_manifest_text(text)
    Path(output).write_text(text, encoding="utf-8")
    log(f"Pinned {len(files)} externals from {repository}@{commit[:12]} into {output}")
    return manifest


def load_manifest_text(text):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as handle:
        handle.write(text)
    try:
        return load_manifest(handle.name)
    finally:
        Path(handle.name).unlink(missing_ok=True)


def _copy_tree_checked(source, destination):
    source = Path(source)
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        safe_relative(relative.as_posix())
        if path.is_symlink():
            raise ValueError(f"links are not allowed in the Neutrino install: {relative}")
        if path.is_dir():
            (destination / relative).mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            (destination / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination / relative)


def extract_neutrino(source, destination):
    """Stage the upstream Neutrino build: a .7z (7z tool, else py7zr), a .zip or an extracted folder."""
    source, destination = Path(source), Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        _copy_tree_checked(source, destination)
    elif source.suffix.lower() == ".zip":
        with zipfile.ZipFile(source) as archive:
            if archive.testzip() is not None:
                raise ValueError("corrupt Neutrino ZIP")
            for info in archive.infolist():
                name = safe_relative(info.filename)
                if (info.external_attr >> 28) == 0xA:
                    raise ValueError("Neutrino archive contains a link")
                if info.is_dir():
                    (destination / name).mkdir(parents=True, exist_ok=True)
                    continue
                (destination / name).parent.mkdir(parents=True, exist_ok=True)
                (destination / name).write_bytes(archive.read(info))
    elif source.suffix.lower() == ".7z":
        extractor = shutil.which("7z") or shutil.which("7za") or shutil.which("7zz")
        if extractor:
            # Inspect members before extraction; never allow an archive to leave its directory.
            listing = subprocess.check_output([extractor, "l", "-slt", str(source)], text=True)
            for line in listing.split("----------", 1)[-1].splitlines():
                if line.startswith("Path = "):
                    safe_relative(line[7:])
                if line.startswith(("Symbolic Link = ", "Hard Link = ")):
                    raise ValueError("Neutrino archive contains a link")
            subprocess.run([extractor, "x", "-y", "-o" + str(destination), str(source)],
                           check=True, stdout=subprocess.DEVNULL)
        else:
            try:
                import py7zr  # optional host fallback; CI installs p7zip-full
            except ImportError as error:
                raise RuntimeError("7z (p7zip-full) or the py7zr module is required for the Neutrino .7z") from error
            with py7zr.SevenZipFile(source) as archive:
                for name in archive.getnames():
                    safe_relative(name)
                if any(info.is_symlink for info in archive.list()):
                    raise ValueError("Neutrino archive contains a link")
                archive.extractall(destination)
    else:
        raise ValueError(f"unsupported Neutrino source: {source}")
    for name in NEUTRINO_REQUIRED:
        path = destination / name
        if not path.is_file() or not path.stat().st_size:
            raise ValueError(f"the Neutrino build lacks {name}; is it the complete latest release?")
    if (destination / "neutrino.elf").read_bytes()[:4] != b"\x7fELF":
        raise ValueError("neutrino.elf is not an ELF")


def _zip_timestamp(epoch):
    instant = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc)
    if not 1980 <= instant.year <= 2099:
        raise ValueError("Commit date is outside the ZIP timestamp range")
    return instant.timetuple()[:6]


def package(app_dir, externals, neutrino, output, revision, epoch, build, manifest_path=MANIFEST, log=print):
    """Write and verify the kit ZIP from package_apps.py's output and the staged externals."""
    packages = _packages()
    manifest = load_manifest(manifest_path)
    app_dir, externals, output = Path(app_dir), Path(externals), Path(output)
    if not revision or any(c not in "0123456789abcdef" for c in revision):
        raise ValueError("Expected a Git revision")
    if not str(build).isdigit():
        raise ValueError("Expected a numeric build number")
    psu = (app_dir / f"{APP_FOLDER}.psu").read_bytes()
    folder, members = packages.inspect_psu(psu)
    if folder != APP_FOLDER or tuple(sorted(members)) != tuple(sorted(APP_MEMBERS)):
        raise ValueError(f"{APP_FOLDER}.psu does not hold the four-file app package")
    with zipfile.ZipFile(app_dir / f"{APP_FOLDER}.zip") as archive:
        zipped = {tuple(safe_relative(name).split("/", 1)): archive.read(name) for name in archive.namelist()}
    if set(zipped) != {(APP_FOLDER, name) for name in APP_MEMBERS} \
            or any(zipped[key] != members[key[1]] for key in zipped):
        raise ValueError(f"{APP_FOLDER}.zip and {APP_FOLDER}.psu differ")
    elf = members[ELF_NAME]
    if elf[:4] != b"\x7fELF":
        raise ValueError("the packaged app ELF is not an ELF")
    if f"boot={ELF_NAME}\n".encode() not in members["title.cfg"]:
        raise ValueError("title.cfg does not name the packaged ELF")
    timestamp = _zip_timestamp(epoch)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kit-") as temp:
        kit = Path(temp) / "kit"
        kit.mkdir()
        for kit_path, entry in manifest["files"].items():
            source = externals / kit_path
            if not source.is_file():
                raise FileNotFoundError(f"external not staged: {kit_path} (run `fetch` first)")
            data = source.read_bytes()
            if sha256(data) != entry["sha256"]:
                raise ValueError(f"staged external differs from its pin: {kit_path}")
            if not data and kit_path not in EMPTY_ALLOWED:
                raise ValueError(f"empty external: {kit_path}")
            (kit / kit_path).parent.mkdir(parents=True, exist_ok=True)
            (kit / kit_path).write_bytes(data)
        if any(p.name.lower() == "bios.bin" for p in kit.rglob("*")):
            raise ValueError("a PS1 BIOS must not be redistributed")
        app_folder = kit / "APPS" / APP_FOLDER
        app_folder.mkdir(parents=True)
        for name in APP_MEMBERS:
            (app_folder / name).write_bytes(members[name])
        (kit / f"{APP_FOLDER}.psu").write_bytes(psu)
        neutrino_dir = kit / "neutrino"
        extract_neutrino(neutrino, neutrino_dir)
        neutrino_source = Path(neutrino)
        for name in SHORTCUT_NAMES:
            shortcut = SHORTCUTS / name
            text = shortcut.read_text(encoding="ascii").replace("\r\n", "\n")
            if not text.startswith("[InternetShortcut]\nURL=https://"):
                raise ValueError(f"malformed shortcut: {shortcut}")
            shutil.copyfile(shortcut, kit / name)
        readme = KIT_README.read_text(encoding="utf-8")
        if "APPS/APP_LAUNCHSTATION" not in readme:
            raise ValueError("ship/KIT-README.txt does not describe the kit")
        (kit / "KIT-README.txt").write_bytes(readme.encode("utf-8"))
        files = {p.relative_to(kit).as_posix(): sha256(p.read_bytes()) for p in sorted(kit.rglob("*")) if p.is_file()}
        kit_manifest = {
            "kit": KIT_NAME,
            "build": int(build),
            "revision": revision,
            "app": {"folder": APP_FOLDER, "elf": ELF_NAME, "elf_sha256": sha256(elf), "psu_sha256": sha256(psu)},
            "externals": {"repository": manifest["source"]["repository"], "commit": manifest["source"]["commit"]},
            "neutrino": {"source": neutrino_source.name,
                         "sha256": sha256(neutrino_source.read_bytes()) if neutrino_source.is_file() else None,
                         "elf_sha256": sha256((neutrino_dir / "neutrino.elf").read_bytes()),
                         "version": (neutrino_dir / "version.txt").read_text(encoding="utf-8", errors="replace").strip()
                         if (neutrino_dir / "version.txt").is_file() else None,
                         "smb": all((neutrino_dir / name).is_file() and (neutrino_dir / name).stat().st_size
                                    for name in NEUTRINO_SMB)},
            "files": files,
        }
        (kit / "KIT-MANIFEST.json").write_text(json.dumps(kit_manifest, indent=2) + "\n", encoding="utf-8")
        partial = output.with_name(output.name + ".part")
        with zipfile.ZipFile(partial, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(kit.rglob("*")):
                if path.is_dir():
                    continue
                name = path.relative_to(kit).as_posix()
                info = zipfile.ZipInfo(name, timestamp)
                info.external_attr = 0o100644 << 16
                # The packed ELFs and ADPCM are already compressed; the rest shrinks.
                info.compress_type = zipfile.ZIP_STORED if path.suffix.upper() in (".ELF", ".ADP") else zipfile.ZIP_DEFLATED
                archive.writestr(info, path.read_bytes())
        partial.replace(output)
    verify(output, psu, manifest)
    log(f"Verified {output.name} ({output.stat().st_size} bytes): {APP_FOLDER} app + PSU, "
        f"{sum(1 for k in manifest['files'] if k.startswith('POPS/'))} POPS files, "
        f"{sum(1 for k in manifest['files'] if k.startswith('EMBER/'))} EMBER files, Neutrino {neutrino_source.name}"
        f" ({kit_manifest['neutrino']['version'] or 'no version.txt'}, SMB modules {'present' if kit_manifest['neutrino']['smb'] else 'absent'})")
    return output


def verify(archive_path, expected_psu, manifest):
    """Validate kit structure, pinned externals, app/PSU consistency and the manifest digests."""
    packages = _packages()
    with zipfile.ZipFile(archive_path) as archive:
        bad = archive.testzip()
        if bad:
            raise ValueError(f"corrupt ZIP entry: {bad}")
        entries = archive.infolist()
        names = [info.filename for info in entries if not info.is_dir()]
        if any(info.is_dir() for info in entries):
            raise ValueError("the kit must not carry directory entries")
        if len(names) != len(set(names)):
            raise ValueError("duplicate ZIP filenames")
        for name in names:
            if safe_relative(name) != name:
                raise ValueError(f"unsafe kit path: {name}")
        present = set(names)
        misplaced = sorted(name for name in present if name.startswith(APP_FOLDER + "/") or "/APP_LAUNCHSTATION-DS/" in name)
        if misplaced:
            raise ValueError(f"the app folder must live under APPS/ and only the standard app ships: {misplaced[:4]}")
        required = {f"APPS/{APP_FOLDER}/{name}" for name in APP_MEMBERS}
        required |= {f"{APP_FOLDER}.psu", "KIT-README.txt", "KIT-MANIFEST.json"}
        required |= {f"neutrino/{name}" for name in NEUTRINO_REQUIRED}
        required |= set(SHORTCUT_NAMES)
        required |= set(manifest["files"])
        missing = sorted(required - present)
        if missing:
            raise ValueError(f"missing kit entries: {missing[:8]}{' ...' if len(missing) > 8 else ''}")
        for name in required - EMPTY_ALLOWED:
            if not archive.getinfo(name).file_size:
                raise ValueError(f"empty required kit entry: {name}")
        if any(name.rsplit("/", 1)[-1].lower() == "bios.bin" for name in present):
            raise ValueError("a PS1 BIOS must not be redistributed")
        for kit_path, entry in manifest["files"].items():
            if sha256(archive.read(kit_path)) != entry["sha256"]:
                raise ValueError(f"kit external differs from its pin: {kit_path}")
        app_prefix = f"APPS/{APP_FOLDER}/"
        app_files = {name[len(app_prefix):]: archive.read(name) for name in present if name.startswith(app_prefix)}
        if set(app_files) != set(APP_MEMBERS):
            raise ValueError(f"APPS/{APP_FOLDER} must hold exactly the four app files, found {sorted(app_files)}")
        psu = archive.read(f"{APP_FOLDER}.psu")
        if psu != expected_psu:
            raise ValueError("the kit PSU differs from the published PSU")
        folder, members = packages.inspect_psu(psu)
        if folder != APP_FOLDER or members != app_files:
            raise ValueError("the kit PSU and its APPS folder differ")
        if archive.read(f"neutrino/neutrino.elf")[:4] != b"\x7fELF" or app_files[ELF_NAME][:4] != b"\x7fELF":
            raise ValueError("a packaged ELF is not an ELF")
        kit_manifest = json.loads(archive.read("KIT-MANIFEST.json").decode("utf-8"))
        listed = kit_manifest["files"]
        if set(listed) != present - {"KIT-MANIFEST.json"}:
            raise ValueError("KIT-MANIFEST.json does not list exactly the kit's files")
        for name, digest in listed.items():
            if sha256(archive.read(name)) != digest:
                raise ValueError(f"KIT-MANIFEST.json digest mismatch: {name}")
        if kit_manifest["app"]["elf_sha256"] != sha256(app_files[ELF_NAME]) or kit_manifest["app"]["psu_sha256"] != sha256(psu):
            raise ValueError("KIT-MANIFEST.json app digests mismatch")
        if kit_manifest["externals"] != {"repository": manifest["source"]["repository"], "commit": manifest["source"]["commit"]}:
            raise ValueError("KIT-MANIFEST.json names a different externals pin")
        if kit_manifest["neutrino"]["smb"] != all(f"neutrino/{name}" in present and archive.getinfo(f"neutrino/{name}").file_size
                                                  for name in NEUTRINO_SMB):
            raise ValueError("KIT-MANIFEST.json misreports the Neutrino SMB modules")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest", type=Path, default=MANIFEST, help="externals pin (default ship/kit-externals.json)")
    commands = parser.add_subparsers(dest="command", required=True)
    fetch = commands.add_parser("fetch", help="stage the pinned POPS/ and EMBER/ externals")
    fetch.add_argument("--cache", type=Path, required=True, help="folder that receives the verified externals")
    fetch.add_argument("--source", type=Path, help="local RiptOPL clone holding the pinned commit (instead of downloading)")
    pack = commands.add_parser("package", help="build and verify LAUNCHSTATION.zip")
    pack.add_argument("--app-dir", type=Path, required=True, help="package_apps.py output (APP_LAUNCHSTATION.zip/.psu)")
    pack.add_argument("--externals", type=Path, required=True, help="folder prepared by `fetch`")
    pack.add_argument("--neutrino", type=Path, required=True, help="upstream Neutrino .7z/.zip, or an extracted folder")
    pack.add_argument("--output", type=Path, required=True, help="kit ZIP to write")
    pack.add_argument("--revision", required=True)
    pack.add_argument("--epoch", type=int, required=True)
    pack.add_argument("--build", required=True, help="build number (the commit count)")
    check = commands.add_parser("verify", help="re-verify an existing kit against a PSU")
    check.add_argument("--kit", type=Path, required=True)
    check.add_argument("--psu", type=Path, required=True)
    pin = commands.add_parser("refresh-pin", help="re-derive the externals pin for a RiptOPL commit")
    pin.add_argument("--commit", required=True, help="full RiptOPL commit hash to pin")
    pin.add_argument("--repository", default="NathanNeurotic/RiptOPL")
    pin.add_argument("--source", type=Path, help="local RiptOPL clone holding that commit (else the GitHub API is used)")
    args = parser.parse_args(argv)
    try:
        if args.command == "fetch":
            fetch_externals(load_manifest(args.manifest), args.cache, args.source)
        elif args.command == "package":
            package(args.app_dir, args.externals, args.neutrino, args.output, args.revision, args.epoch, args.build, args.manifest)
        elif args.command == "verify":
            verify(args.kit, args.psu.read_bytes(), load_manifest(args.manifest))
            print(f"Verified {args.kit}")
        elif args.command == "refresh-pin":
            refresh_pin(args.repository, args.commit, args.source, args.manifest)
    except (ValueError, FileNotFoundError, RuntimeError, subprocess.CalledProcessError, urllib.error.URLError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
