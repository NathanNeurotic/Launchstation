"""Stamp ps2build artifacts with this checkout's committed source identity."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def revision():
    top = subprocess.run(['git', 'rev-parse', '--show-toplevel'],
                         cwd=ROOT, capture_output=True, text=True)
    if top.returncode or Path(top.stdout.strip()).resolve() != ROOT.resolve():
        return None
    # Release ZIP names use the full-history commit count. A shallow count
    # would label the executable differently from its published package.
    shallow = subprocess.run(['git', 'rev-parse', '--is-shallow-repository'],
                             cwd=ROOT, capture_output=True, text=True)
    if shallow.returncode or shallow.stdout.strip() != 'false':
        return None
    result = subprocess.run(['git', 'show', '-s', '--format=%H%n%cs', 'HEAD'],
                            cwd=ROOT, capture_output=True, text=True)
    lines = result.stdout.splitlines()
    if result.returncode or len(lines) != 2:
        return None
    sha, date = lines
    if not re.fullmatch(r'[0-9a-f]{40}', sha) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}', date):
        return None
    count = subprocess.run(['git', 'rev-list', '--count', 'HEAD'],
                           cwd=ROOT, capture_output=True, text=True)
    build = count.stdout.strip()
    if count.returncode or not re.fullmatch(r'[1-9][0-9]*', build):
        return None
    return sha[:12], date, build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'githash.h')
    parser.add_argument('--require-git', action='store_true')
    args = parser.parse_args()
    identity = revision()
    if identity is None and args.require_git:
        parser.error('A Git revision is required for an identified build')
    sha, date, build = identity or ('local', 'unknown', 'local')
    content = (f'#ifndef ULE_VERDATE\n#define ULE_VERDATE "{date}"\n#endif\n'
               f'#ifndef GIT_HASH\n#define GIT_HASH "{sha}"\n#endif\n'
               f'#ifndef LAUNCHSTATION_BUILD\n#define LAUNCHSTATION_BUILD "{build}"\n#endif\n')
    if not args.output.exists() or args.output.read_text() != content:
        args.output.write_text(content, encoding='ascii')
    print(f'Build identity: Launchstation {build}, {sha} ({date})')


if __name__ == '__main__':
    main()
