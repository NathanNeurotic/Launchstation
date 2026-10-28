#!/bin/sh
# Install the pinned ps2build SDK into DEST without depending on git.techwritescode.dev being up.
#
# Usage: ps2build_sdk.sh [--llvm] DEST
#
# The pin file (PS2BUILD_SDK_PIN, default .github/ps2build-sdk.env) names the exact component versions
# and, once the mirror has a release for them, its tag and the SHA-256 of that release's SHA256SUMS.
# Sources, in order:
#   1. DEST already holds exactly the pinned versions (an actions/cache restore): use it.
#   2. The NathanNeurotic mirror release: every file is checked against SHA256SUMS, whose own hash is pinned.
#   3. The official server, pinned per component with "ps2build update <component> --version <tag>".
#      Never "latest": install.sh and setup-sdk always take the newest release, which is not reproducible.
#      Set PS2BUILD_SDK_NO_FALLBACK=1 to fail instead.
# --llvm also installs the LLVM toolchain. On GitHub Actions DEST is added to PATH and
# PS2BUILD_SDK_SOURCE (cache, mirror or official) to the environment.
set -eu

LLVM=0
if [ "${1:-}" = "--llvm" ]; then
    LLVM=1
    shift
fi
[ $# -eq 1 ] || { echo "usage: $0 [--llvm] DEST" >&2; exit 2; }
DEST=$1
PIN=${PS2BUILD_SDK_PIN:-.github/ps2build-sdk.env}
OFFICIAL=https://git.techwritescode.dev

[ -f "$PIN" ] || { echo "error: pin file $PIN not found" >&2; exit 2; }
PS2BUILD_SDK_MIRROR= PS2BUILD_SDK_TAG= PS2BUILD_SDK_SUMS_SHA256= PS2BUILD_COMPONENTS= PS2BUILD_LLVM_TOOLCHAIN=
# shellcheck disable=SC1090
case $PIN in /*) . "$PIN" ;; *) . "./$PIN" ;; esac
[ -n "$PS2BUILD_COMPONENTS" ] || { echo "error: $PIN sets no PS2BUILD_COMPONENTS" >&2; exit 2; }
if [ "$LLVM" = 1 ] && [ -z "$PS2BUILD_LLVM_TOOLCHAIN" ]; then
    echo "error: --llvm but $PIN sets no PS2BUILD_LLVM_TOOLCHAIN" >&2
    exit 2
fi

log() { echo "ps2build-sdk: $*"; }

version_of() { # component -> pinned version
    for pair in $PS2BUILD_COMPONENTS; do
        [ "${pair%%=*}" = "$1" ] && { echo "${pair#*=}"; return 0; }
    done
    return 1
}

wanted() { # "component version" lines this job needs
    for pair in $PS2BUILD_COMPONENTS; do
        echo "${pair%%=*} ${pair#*=}"
    done
    [ "$LLVM" = 1 ] && echo "llvm-toolchain $PS2BUILD_LLVM_TOOLCHAIN"
    return 0
}

installed_ok() { # DEST holds every wanted component at its pinned version
    [ -x "$DEST/ps2build" ] && [ -f "$DEST/versions.yaml" ] || return 1
    wanted | while read -r name version; do
        if [ "$name" = ps2build ]; then
            "$DEST/ps2build" --version 2>/dev/null | grep -qx "ps2build $version" || { echo missing; break; }
        else
            grep -Eq "^ +$name: $version\$" "$DEST/versions.yaml" || { echo missing; break; }
        fi
    done | grep -q missing && return 1
    [ "$LLVM" = 0 ] || [ -f "$DEST/llvm-toolchain/ee/bin/mips64r5900el-ps2-elf-clang.cfg" ] || return 1
    return 0
}

reset_dest() { # never delete something that is not an SDK install
    if [ -e "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
        if [ ! -f "$DEST/versions.yaml" ] && [ ! -x "$DEST/ps2build" ]; then
            echo "error: $DEST is not empty and is not a ps2build SDK; refusing to replace it" >&2
            exit 1
        fi
        log "replacing the SDK in $DEST (not the pinned versions)"
        rm -rf "$DEST"
    fi
    mkdir -p "$DEST"
}

fetch() { # url file
    # --speed-limit: a stalled transfer is aborted (and retried) instead of hanging the job.
    curl -sSfL --retry 6 --retry-delay 5 --retry-all-errors --connect-timeout 20 --speed-limit 1024 --speed-time 60 "$1" -o "$2"
}

sha256() { sha256sum "$1" | cut -d' ' -f1; }

from_mirror() {
    [ -n "$PS2BUILD_SDK_TAG" ] && [ -n "$PS2BUILD_SDK_SUMS_SHA256" ] || {
        log "the pin names no mirror release yet"
        return 1
    }
    base=${PS2BUILD_SDK_MIRROR_URL:-https://github.com/$PS2BUILD_SDK_MIRROR/releases/download/$PS2BUILD_SDK_TAG}
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    fetch "$base/SHA256SUMS" "$work/SHA256SUMS" || { log "cannot download $base/SHA256SUMS"; return 1; }
    got=$(sha256 "$work/SHA256SUMS")
    if [ "$got" != "$PS2BUILD_SDK_SUMS_SHA256" ]; then
        echo "error: mirror SHA256SUMS is $got, the pin says $PS2BUILD_SDK_SUMS_SHA256" >&2
        return 1
    fi
    parts=base
    [ "$LLVM" = 1 ] && parts="base llvm"
    reset_dest
    for part in $parts; do
        name=$(sed -n "s/^[0-9a-f]\{64\}  \(ps2build-sdk-.*-linux-amd64-$part\.tar\.zst\)\$/\1/p" "$work/SHA256SUMS")
        [ -n "$name" ] || { echo "error: SHA256SUMS lists no $part archive" >&2; return 1; }
        log "downloading $name"
        fetch "$base/$name" "$work/$name" || { log "cannot download $name"; return 1; }
        (cd "$work" && grep "  $name\$" SHA256SUMS | sha256sum -c --quiet -) || {
            echo "error: $name does not match SHA256SUMS" >&2
            return 1
        }
        tar --zstd -xf "$work/$name" -C "$DEST"
        rm -f "$work/$name"
    done
    installed_ok || { echo "error: the mirror archives do not hold the pinned versions" >&2; return 1; }
}

update_component() { # name version, with retries: the official host has multi-minute outages
    for wait in 20 60 0; do
        # ps2build has no download timeout of its own; a stalled transfer would hang the job.
        timeout "${PS2BUILD_SDK_UPDATE_TIMEOUT:-1200}" "$DEST/ps2build" update "$1" --version "$2" && return 0
        [ "$wait" = 0 ] && break
        log "update $1 $2 failed; retrying in $wait s"
        sleep "$wait"
    done
    return 1
}

from_official() {
    reset_dest
    version=$(version_of ps2build)
    log "downloading ps2build $version from $OFFICIAL"
    fetch "$OFFICIAL/ps2/ps2build/releases/download/$version/ps2build-$version-linux-amd64" "$DEST/ps2build"
    chmod +x "$DEST/ps2build"
    wanted | while read -r name version; do
        [ "$name" = ps2build ] && continue
        update_component "$name" "$version" || exit 1
    done
    installed_ok || { echo "error: the official install does not hold the pinned versions" >&2; return 1; }
}

if installed_ok; then
    source=cache
elif (from_mirror); then
    source=mirror
elif [ "${PS2BUILD_SDK_NO_FALLBACK:-0}" = 1 ]; then
    echo "error: the mirror failed and PS2BUILD_SDK_NO_FALLBACK=1" >&2
    exit 1
else
    [ -n "${GITHUB_ACTIONS:-}" ] && echo "::warning::ps2build SDK installed from $OFFICIAL, not the checksummed mirror"
    from_official
    source=official
fi
rm -rf "$DEST/.update-staging"

log "SDK ready in $DEST (from $source)"
if [ -n "${GITHUB_ENV:-}" ]; then
    echo "$DEST" >> "$GITHUB_PATH"
    echo "PS2BUILD_SDK_SOURCE=$source" >> "$GITHUB_ENV"
fi
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    echo "ps2build SDK: \`${PS2BUILD_SDK_TAG:-unmirrored pin}\` from **$source**" >> "$GITHUB_STEP_SUMMARY"
fi
