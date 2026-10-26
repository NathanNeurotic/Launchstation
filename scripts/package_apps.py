"""Build the four installable Launchstation app archives and four loose ELFs.

PSU records/padding follow RiptOPL's package_release_app.py and PSUManager:
512-byte root/dot/dotdot/file records, with file payloads padded to 1024 bytes.
ZIPs and PSUs contain the same four direct app files, under the same folder name.
"""
import argparse
import datetime
from pathlib import Path
import struct
import zipfile

ENTRY = struct.Struct("<HHI8sII8sI28s448s")
VARIANTS = (("APP_LAUNCHSTATION", "LAUNCHSTATION.ELF", "Launchstation"),
            ("APP_LAUNCHSTATION-DS", "LAUNCHSTATION-DS.ELF", "Launchstation (DualShock)"))

# Loose ELF downloads: (file name, variant index into VARIANTS). BOOT.ELF and
# BOOT2.ELF are copies of the standard build for boot-path installs.
LOOSE_ELFS = (("LAUNCHSTATION.ELF", 0), ("LAUNCHSTATION-DS.ELF", 1),
              ("BOOT.ELF", 0), ("BOOT2.ELF", 0))

ASSET_ROOT = Path(__file__).resolve().parents[1] / "res"


def bundled_sidecars():
    """Provide the runtime files beside the ELF in both install formats."""
    ca = (ASSET_ROOT / "WLE-UPDATE-CA.PEM").read_bytes()
    bgm = (ASSET_ROOT / "music.sad").read_bytes()
    cert_start = b"-----BEGIN CERTIFICATE-----"
    cert_end = b"-----END CERTIFICATE-----"
    if ca.count(cert_start) < 2 or ca.count(cert_start) != ca.count(cert_end):
        raise ValueError("The updater CA bundle is missing or incomplete")
    if not 32 <= len(bgm) <= 1024 * 1024 or len(bgm) % 16 or bgm[:8] != b"APCM\x01\x01\x01\x00":
        raise ValueError("The BGM asset is not a valid PS2 ADPCM sidecar")
    return {"WLE-UPDATE-CA.PEM": ca, "BGM.ADP": bgm}


def record(name, size, mode, date):
    encoded = name.encode("ascii")
    if len(encoded) > 31 or "/" in name or "\\" in name:
        raise ValueError("Invalid memory-card name")
    return ENTRY.pack(mode, 0, size, date, 0, 0, date, 0, bytes(28), encoded.ljust(448, b"\0"))


def make_psu(folder, members, date):
    data = bytearray(record(folder, len(members) + 2, 0x8027, date))
    data += record(".", 0, 0x8027, date) + record("..", 0, 0x8027, date)
    for name, payload in members.items():
        data += record(name, len(payload), 0x8017, date)
        data += payload + bytes((-len(payload)) % 1024)
    if len(data) >= 8 * 1024 * 1024:
        raise ValueError("App save exceeds an 8 MB memory card")
    return data


def inspect_psu(data):
    if len(data) < 1536:
        raise ValueError("Truncated PSU directory records")
    records = [ENTRY.unpack_from(data, offset) for offset in (0, 512, 1024)]
    def name(entry):
        return entry[-1].split(b"\0", 1)[0].decode("ascii")
    if any(r[0] != 0x8027 for r in records) or name(records[1]) != "." or name(records[2]) != "..":
        raise ValueError("Invalid PSU directory records")
    position, members = 1536, {}
    for _ in range(records[0][2] - 2):
        if position + 512 > len(data):
            raise ValueError("Truncated PSU file record")
        entry = ENTRY.unpack_from(data, position)
        key = name(entry)
        start, end = position + 512, position + 512 + entry[2]
        if entry[0] != 0x8017 or key in members or end > len(data):
            raise ValueError("Invalid PSU file record")
        members[key] = data[start:end]
        position = start + (entry[2] + 1023) // 1024 * 1024
    if position != len(data):
        raise ValueError("Unexpected trailing PSU bytes")
    return name(records[0]), members


def package(standard, dualshock, output, revision, epoch):
    if not revision or not all(c in "0123456789abcdef" for c in revision):
        raise ValueError("Expected a Git revision")
    instant = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc)
    if not 2000 <= instant.year <= 2099:
        raise ValueError("Commit date is outside the PS2 timestamp range")
    date = struct.pack("<BBBBBBH", 0, instant.second, instant.minute, instant.hour,
                       instant.day, instant.month, instant.year)
    output.mkdir(parents=True, exist_ok=True)
    elfs = []
    for (folder, elf_name, title), source in zip(VARIANTS, (standard, dualshock)):
        elf = source.read_bytes()
        elfs.append(elf)
        if len(elf) < 52 or elf[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<H", elf, 18)[0] != 8:
            raise ValueError(f"Not a little-endian ELF32 MIPS executable: {source}")
        cfg = (f"title=[APP] {title}\nboot={elf_name}\nTitle={title}\nCfgVersion=8\n"
               f"Version={revision[:8]}\nPackage=1.0\nRelease=Preview\n"
               "Developer=github.com/nathanneurotic/\nGenre=Homebrew\n"
               "Description=PlayStation 2 file manager, launcher and device toolkit.\n"
               "PlayersText=1\nSource=github.com/nathanneurotic/Launchstation/\n").encode()
        members = {elf_name: elf, "title.cfg": cfg, **bundled_sidecars()}
        psu = make_psu(folder, members, date)
        assert inspect_psu(psu) == (folder, members)
        (output / (folder + ".psu")).write_bytes(psu)
        with zipfile.ZipFile(output / (folder + ".zip"), "w") as archive:
            for name, payload in members.items():
                info = zipfile.ZipInfo(folder + "/" + name, instant.timetuple()[:6])
                info.compress_type = zipfile.ZIP_STORED  # The ELF is already ps2pack compressed.
                info.external_attr = 0o100644 << 16
                archive.writestr(info, payload)
        with zipfile.ZipFile(output / (folder + ".zip")) as archive:
            assert archive.testzip() is None
            assert {name.split("/", 1)[1]: archive.read(name) for name in archive.namelist()} == members
        print(f"Verified {folder}.psu/.zip: {elf_name}, title.cfg, updater CA, and BGM.ADP")
    for name, variant in LOOSE_ELFS:
        (output / name).write_bytes(elfs[variant])
        assert (output / name).read_bytes() == elfs[variant]
        print(f"Verified {name}: identical to the {VARIANTS[variant][1]} inside the packages")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--standard", type=Path, required=True)
    parser.add_argument("--dualshock", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--epoch", type=int, required=True)
    args = parser.parse_args()
    package(args.standard, args.dualshock, args.output, args.revision, args.epoch)
