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
for asset in ("APP_LAUNCHSTATION.psu", '"APP_LAUNCHSTATION-${build}.zip"',
              "APP_LAUNCHSTATION-DS.psu", '"APP_LAUNCHSTATION-DS-${build}.zip"',
              "LAUNCHSTATION.ELF", "LAUNCHSTATION-DS.ELF", "BOOT.ELF", "BOOT2.ELF",
              '"LAUNCHSTATION-${build}.zip"'):
    assert asset in publish
# The full kit is built and verified before the publish step, from the same app
# packages, the pinned externals and the latest Neutrino release.
assert "python3 tests/test_kit_package.py" in workflow
assert "scripts/package_kit.py fetch --cache build/kit-externals" in workflow
assert "scripts/package_kit.py package" in workflow and "--output artifacts/LAUNCHSTATION.zip" in workflow
assert workflow.index("--output artifacts/LAUNCHSTATION.zip") < workflow.index(marker)
assert "gh release download latest --repo ps2max32/neutrino" in workflow
assert "LAUNCHSTATION.zip) stale=1" in publish and '"LAUNCHSTATION-${number}.zip"' in publish
# Every ZIP name ends with the build number, the notes state it, and the previous
# build's ZIPs are removed only after the release is published.
assert 'build="$(git rev-list --count HEAD)"' in publish
assert '--version "${build}"' in publish
assert 'title="Launchstation Rolling ${build} - ' in publish
assert publish.index("--draft=false --prerelease=false --latest") < publish.index("gh release delete-asset")

# Fail the PR before shipping if the release's embedded shell script has
# malformed Bash syntax (rather than discovering that after compiling ELFs).
start = publish.index("        run: |\n") + len("        run: |\n")
script = publish[start:]
assert all(not line.strip() or line.startswith("          ") for line in script.splitlines())
script = "\n".join(line[10:] for line in script.splitlines()) + "\n"
subprocess.run(["bash", "-n"], input=script.encode("utf-8"), check=True)
print("PASS: auto-rolling master trigger, existing stable URLs, ancestry and assets, Bash syntax")
