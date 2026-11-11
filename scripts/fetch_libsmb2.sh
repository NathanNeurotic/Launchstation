#!/bin/sh
# Fetch the libsmb2 source the SMB file server (iop/smbd) is built from, into .smbd-deps/libsmb2.
# libsmb2 is Ronnie Sahlberg's, LGPL-2.1-or-later (.smbd-deps/libsmb2/COPYING). It is not
# vendored: this pins one commit of upstream master and applies iop/smbd/libsmb2-*.patch.
# A checkout already at that commit with those patches is left alone. That master's server
# has stop_requested/listener_ready, which smbd needs.
set -eu

LIBSMB2_REPO=${LIBSMB2_REPO:-https://github.com/sahlberg/libsmb2.git}
LIBSMB2_COMMIT=a52c1571a1ff223bde5bb3df9fec7af5ce3855d4 # master, 2026-11

root=$(cd "$(dirname "$0")/.." && pwd)
dest="$root/.smbd-deps/libsmb2"

patches=$(ls "$root"/iop/smbd/libsmb2-*.patch)
stamp=$(cat $patches | git hash-object --stdin)
if [ "$(git -C "$dest" rev-parse HEAD 2>/dev/null || true)" = "$LIBSMB2_COMMIT" ] &&
    [ "$(cat "$dest/.smbd-patches" 2>/dev/null || true)" = "$stamp" ]; then
    exit 0
fi
rm -rf "$dest"
mkdir -p "$dest"
git -C "$dest" init -q
git -C "$dest" fetch -q --depth 1 "$LIBSMB2_REPO" "$LIBSMB2_COMMIT"
git -C "$dest" -c advice.detachedHead=false checkout -q FETCH_HEAD
[ "$(git -C "$dest" rev-parse HEAD)" = "$LIBSMB2_COMMIT" ] || { echo "libsmb2: wrong commit" >&2; exit 1; }
for p in $patches; do
    git -C "$dest" apply "$p"
done
echo "$stamp" >"$dest/.smbd-patches"
echo "libsmb2 $LIBSMB2_COMMIT in $dest"
