"""Ember Watch: moves the kit's Ember pin by itself, only to RiptOPL's official ember.elf, never via issues.

Host-side workflow test; it runs nothing.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
watch = (root / ".github/workflows/ember-watch.yml").read_text()
compile_workflow = (root / ".github/workflows/compile.yml").read_text()

assert "python3 tests/test_ember_watch.py" in compile_workflow
assert "EMBER_REPO: Gageformer/Ember" in watch and "RIPTOPL_REPO: NathanNeurotic/RiptOPL" in watch
assert "issue" not in watch.replace("never opens issues", "")
# The pin only moves when RiptOPL's ember.elf is the official build, and only after fetch + tests pass.
order = [watch.index(step) for step in (
    'if [ "$GOT" != "$SHA" ]', "fetch --cache", "cp \"$NEW\" ship/kit-externals.json",
    "python3 tests/test_kit_package.py", "git commit", "git push origin HEAD:master",
    "gh workflow run compile.yml")]
assert order == sorted(order), order
assert "git add ship/kit-externals.json\n" in watch and "git add -A" not in watch
# A GITHUB_TOKEN push starts no workflow, so the watch starts Automated-Build itself.
assert "contents: write" in watch and "actions: write" in watch
print("Ember Watch: pins only the official ember.elf, checks before committing, rebuilds the kit")
