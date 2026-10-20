"""Verify concise public notes, release links, first-parent summaries and hashes."""
from pathlib import Path
import hashlib
import importlib.util
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_notes", root / "scripts/release_notes.py")
notes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(notes)
repo = "NathanNeurotic/Launchstation"
revision, previous = "a" * 40, "b" * 40
log = (
    revision + "\0Merge pull request #184 from owner/branch\n\nfix(boot): hide startup driver status\n\x1e\n"
    + previous + "\0Merge pull request #183: Simplify [release] notes\n\nInternal merge metadata\n\x1e\n"
)
changes = notes.changes_from_log(log, repo)
public = notes.rolling_changelog("# Changes\n## Rolling\n\n- **Files:** safe copying.\n- Clear progress.\n\n## Older\n- Old entry.\n")
assert public == ["- **Files:** safe copying.", "- Clear progress."]
assert notes.rolling_changelog("# Empty") == []
assert len(changes) == 2
assert "Hide startup driver status" in changes[0] and "/pull/184" in changes[0]
assert r"Simplify \[release\] notes" in changes[1] and "/pull/183" in changes[1]
assert "Internal merge metadata" not in "\n".join(changes)
digests = {name: hashlib.sha256(name.encode()).hexdigest() for name in notes.ASSETS}
body = notes.render(repo, revision, previous, 123, changes, digests)
visible, details = body.split("<details>", 1)
assert "## Downloads" in visible and "## Changelog" in visible
assert "Recently merged pull requests" not in body and "Co-authored-by" not in body
assert "SHA-256" not in visible and "Build verification and SHA-256 checksums" in details
assert "does not establish hardware compatibility" in visible
assert f"/compare/{previous}...{revision}" in body and "/actions/runs/123" in details
for name, digest in digests.items():
    assert f"/releases/download/latest/{name}" in visible
    assert digest in details and digest not in visible
empty = notes.render(repo, revision, "", 123, [], digests)
assert "Rebuild of the current revision" in empty and f"/commits/{revision}" in empty

# Exercise the CLI with real git history and actual package bytes.
with tempfile.TemporaryDirectory(prefix="release-notes-") as folder:
    path = Path(folder)
    for name in notes.ASSETS:
        (path / name).write_bytes(("fixture:" + name).encode())
    output = path / "notes.md"
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    args = [sys.executable, str(root / "scripts/release_notes.py"), "--repo", repo,
            "--revision", head, "--previous", head, "--run-id", "123",
            "--assets-dir", str(path), "--output", str(output)]
    subprocess.run(args, cwd=root, check=True)
    generated = output.read_text()
    summary = subprocess.run(["git", "show", f"{head}:CHANGELOG.md"], cwd=root, capture_output=True, text=True)
    expected = notes.rolling_changelog(summary.stdout)
    if expected:
        assert all(line in generated for line in expected)
        assert "Rebuild of the current revision" not in generated
    else:
        assert "Rebuild of the current revision" in generated
    for name in notes.ASSETS:
        assert hashlib.sha256((path / name).read_bytes()).hexdigest() in generated
    (path / notes.ASSETS[0]).unlink()
    assert subprocess.run(args, cwd=root, capture_output=True).returncode != 0

# Unchanged summaries cannot hide subsequent commits; same-SHA reruns keep them.
original_reader = notes.revision_changelog
try:
    notes.revision_changelog = lambda sha: ["- Maintained summary"]
    assert notes.release_changes(repo, revision, revision, "") == ["- Maintained summary"]
    assert notes.release_changes(repo, revision, previous, log) == changes
    notes.revision_changelog = lambda sha: ["- New summary"] if sha == revision else ["- Old summary"]
    assert notes.release_changes(repo, revision, previous, log) == ["- New summary"]
    assert notes.release_changes(repo, revision, "", log) == ["- New summary"]
    notes.revision_changelog = lambda sha: []
    assert notes.release_changes(repo, revision, previous, log) == changes
finally:
    notes.revision_changelog = original_reader

workflow = (root / ".github/workflows/compile.yml").read_text()
assert "python3 scripts/release_notes.py" in workflow
assert 'gh pr list' not in workflow and "git log --format='- %s (%h)'" not in workflow
print("PASS: concise release notes, first-parent PR summaries, stable downloads, folded hashes and CLI validation")
