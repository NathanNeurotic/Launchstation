"""Create a small deterministic copy fixture ZIP or verify a returned copy."""
import argparse
import hashlib
from pathlib import Path
import zipfile

CASES = {
    'EMPTY.BIN': 0, 'ONE.BIN': 1,
    'B16383.BIN': 16383, 'B16384.BIN': 16384, 'B16385.BIN': 16385,
    'B65535.BIN': 65535, 'B65536.BIN': 65536, 'B65537.BIN': 65537,
    'NESTED/CHILD.BIN': 257,
}


def payload(name, size):
    return hashlib.shake_256(('Launchstation copy fixture v1:' + name).encode('ascii')).digest(size)


def manifest():
    return ''.join(f'{hashlib.sha256(payload(name, size)).hexdigest()}  {name}\n'
                   for name, size in CASES.items())


INSTRUCTIONS = """Launchstation copy test - scratch files only

1. Extract LS-COPY-TEST. Use the same source files on UDPFS and USB.
2. Copy LS-COPY-TEST into a scratch directory on mc0. Keep real saves separate.
3. Repeat the paste over the existing files; then repeat it once more.
4. Controls: USB -> mc0, and UDPFS -> USB. Test mc1 separately if available.
5. Copy the result back to a NEW PC directory and verify:
   python scripts/copy_test_pack.py --verify /path/to/returned/LS-COPY-TEST
   Or run sha256sum -c SHA256SUMS inside the returned folder.
6. On failure, photograph the entire error including stage, code and build.
   Record source/destination, whether a file already existed, and the server build.

Payload: 246018 bytes; normal MC filesystem overhead is additional.
Replacement requires room for old and new files. Avoid delete/move for this pass.
The files are data, not executable ELFs. Test an ELF separately from the app ZIP;
never overwrite the running launcher or your only known-good boot ELF.
LS-COPY-*.TMP / LS-OLD-*.BAK are recovery files: preserve them after failures.
No console or adapter behavior is established by a PC verification result.
"""


def create(output):
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        contents = {'README.txt': INSTRUCTIONS.encode(),
                    'LS-COPY-TEST/SHA256SUMS': manifest().encode()}
        contents.update({f'LS-COPY-TEST/{name}': payload(name, size) for name, size in CASES.items()})
        for name, data in contents.items():
            entry = zipfile.ZipInfo(name, (2000, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)


def verify(folder):
    ok = True
    # Compare to this generator's expected bytes, not a potentially damaged manifest.
    for name, size in CASES.items():
        path = folder / name
        expected = hashlib.sha256(payload(name, size)).hexdigest()
        valid = path.is_file() and path.stat().st_size == size and hashlib.sha256(path.read_bytes()).hexdigest() == expected
        print(f'{"PASS" if valid else "FAIL"} {name} ({size} bytes)')
        ok &= valid
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--output', type=Path)
    group.add_argument('--verify', type=Path)
    args = parser.parse_args()
    if args.output:
        create(args.output)
        print(f'Created {args.output}: {len(CASES)} fixtures, {sum(CASES.values())} payload bytes')
    elif not verify(args.verify):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
