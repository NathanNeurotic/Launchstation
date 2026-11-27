#!/bin/sh
# Run once after checking out the claude-audit-findings branch (cloud or fresh clone).
# Creates src/Launchstation next to this script: the audited commit as a detached git worktree (ignored by git).
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(git -C "$here" rev-parse --show-toplevel)
sha=8155d14cb69bffae7c0827cde4ab3eab312b8755
if [ ! -e "$here/src/Launchstation" ]; then
  git -C "$top" cat-file -e "$sha^{commit}" 2>/dev/null || git -C "$top" fetch --quiet origin "$sha" || git -C "$top" fetch --quiet --unshallow origin
  git -C "$top" worktree add --quiet --detach "$here/src/Launchstation" "$sha"
fi
echo "src/Launchstation at $(git -C "$here/src/Launchstation" rev-parse HEAD)"
