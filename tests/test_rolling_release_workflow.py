"""Protect Launchstation's automatic rolling-release publication contract.

This is a host-side workflow test. It does not publish anything.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text()

assert "push:\n    branches: [master, main]" in workflow
assert "pull_request:" in workflow
assert "contents: write" in workflow
assert "cancel-in-progress: true" in workflow
assert "python3 tests/test_rolling_release_workflow.py" in workflow

marker = "      - name: Publish rolling release from verified master build\n"
assert workflow.count(marker) == 1
publish = workflow[workflow.index(marker):]
assert ("if: (github.event_name == 'push' || github.event_name == "
        "'workflow_dispatch') && github.ref == 'refs/heads/master'") in publish
assert "contains(github.event.head_commit.message" not in workflow
assert 'tag="latest"' in publish  # Preserve native updater/website URLs.
assert "gh release upload" in publish
assert "gh release edit" in publish
assert "--draft=false --prerelease=false --latest" in publish
assert "git merge-base --is-ancestor" in publish
assert "gh api --method PATCH" in publish
assert publish.index('for asset in "${assets[@]}"') < publish.index("gh release upload")
assert publish.index("gh release upload") < publish.index("gh api --method PATCH")
assert publish.index("gh api --method PATCH") < publish.index("gh release edit")
# Re-publishing refreshes the release page date (published_at never changes on
# a plain edit), and must happen after the tag moves, before the final publish.
assert "gh release edit \"${tag}\" --draft=true" in publish
assert publish.index("--draft=true") < publish.index("--draft=false --prerelease=false --latest")
assert publish.index("gh api --method PATCH") < publish.index("--draft=true")
for asset in ("APP_LAUNCHSTATION.psu", "APP_LAUNCHSTATION.zip",
              "APP_LAUNCHSTATION-DS.psu", "APP_LAUNCHSTATION-DS.zip",
              "LAUNCHSTATION.ELF", "LAUNCHSTATION-DS.ELF", "BOOT.ELF", "BOOT2.ELF"):
    assert asset in publish

# Fail the PR before shipping if the release's embedded shell script has
# malformed Bash syntax (rather than discovering that after compiling ELFs).
start = publish.index("        run: |\n") + len("        run: |\n")
script = publish[start:]
assert all(not line.strip() or line.startswith("          ") for line in script.splitlines())
script = "\n".join(line[10:] for line in script.splitlines()) + "\n"
subprocess.run(["bash", "-n"], input=script.encode("utf-8"), check=True)
print("PASS: auto-rolling master trigger, existing stable URLs, ancestry and assets, Bash syntax")
