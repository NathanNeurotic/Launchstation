#!/bin/sh
# Install the pinned ps2build SDK into DEST, from git.techwritescode.dev or, when it is down, our mirror.
#
# Usage: ps2build_sdk.sh [--llvm] DEST
#
# The pin file (PS2BUILD_SDK_PIN, default .github/ps2build-sdk.env) names the exact component versions
# and, once the mirror has a release for them, its tag and the SHA-256 of that release's SHA256SUMS.
# Sources, in order:
#   1. DEST already holds exactly the pinned versions (an actions/cache restore): use it.
#   2. The official server, pinned per component with "ps2build update <component> --version <tag>".
#      Never "latest": install.sh and setup-sdk always take the newest release, which is not reproducible.
#      The whole attempt gets PS2BUILD_SDK_OFFICIAL_BUDGET seconds (default 900), each component one retry,
#      so an outage or a stalled download falls through to the mirror instead of eating the job.
#   3. The NathanNeurotic mirror release, only when the official install fails: every file is checked
#      against SHA256SUMS, whose own hash is pinned. Set PS2BUILD_SDK_NO_FALLBACK=1 to fail instead.
# --llvm also installs the LLVM toolchain. On GitHub Actions DEST is added to PATH and
# PS2BUILD_SDK_SOURCE (cache, mirror or official) to the environment.
set -eu

OFFICIAL_ONLY=0
if [ "${1:-}" = "--official-only" ]; then # internal: the official attempt, run under one timeout
    OFFICIAL_ONLY=1
    shift
fi
LLVM=0
if [ "${1:-}" = "--llvm" ]; then
    LLVM=1
    shift
fi
[ $# -eq 1 ] || { echo "usage: $0 [--llvm] DEST" >&2; exit 2; }
DEST=$1
PIN=${PS2BUILD_SDK_PIN:-.github/ps2build-sdk.env}
OFFICIAL=${PS2BUILD_SDK_OFFICIAL_URL:-https://git.techwritescode.dev}

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
        # ps2build may be a partial download from an interrupted official attempt, so not -x.
        if [ ! -f "$DEST/versions.yaml" ] && [ ! -f "$DEST/ps2build" ]; then
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

update_component() { # name version, one retry: the mirror covers a longer outage
    for wait in 20 0; do
        # ps2build has no download timeout of its own; a stalled transfer would hang the job.
        # --foreground keeps it in our process group, so the overall budget can stop it too.
        timeout --foreground "${PS2BUILD_SDK_UPDATE_TIMEOUT:-600}" "$DEST/ps2build" update "$1" --version "$2" && return 0
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
    fetch "$OFFICIAL/ps2/ps2build/releases/download/$version/ps2build-$version-linux-amd64" "$DEST/ps2build" || return 1
    chmod +x "$DEST/ps2build"
    wanted | while read -r name version; do
        [ "$name" = ps2build ] && continue
        update_component "$name" "$version" || exit 1
    done || return 1
    installed_ok || { echo "error: the official install does not hold the pinned versions" >&2; return 1; }
}

if [ "$OFFICIAL_ONLY" = 1 ]; then
    echo $$ > "$PS2BUILD_SDK_PGID_FILE"
    from_official
    exit
fi

official() { # the official attempt in its own process group, so a timeout also stops ps2build and curl
    PS2BUILD_SDK_PGID_FILE=$(mktemp)
    export PS2BUILD_SDK_PGID_FILE
    llvm_flag=
    [ "$LLVM" = 1 ] && llvm_flag=--llvm
    status=0
    timeout "${PS2BUILD_SDK_OFFICIAL_BUDGET:-900}" setsid sh "$0" --official-only $llvm_flag "$DEST" || status=$?
    if [ "$status" != 0 ]; then
        [ "$status" = 124 ] && log "the official install took longer than ${PS2BUILD_SDK_OFFICIAL_BUDGET:-900} s"
        pgid=$(cat "$PS2BUILD_SDK_PGID_FILE" 2>/dev/null || true)
        # Nothing left over may keep writing into DEST while the mirror replaces it.
        [ -n "$pgid" ] && env kill -KILL -- "-$pgid" 2>/dev/null || true  # dash's kill takes no process group
    fi
    rm -f "$PS2BUILD_SDK_PGID_FILE"
    return "$status"
}

if installed_ok; then
    source=cache
elif official; then
    source=official
elif [ "${PS2BUILD_SDK_NO_FALLBACK:-0}" = 1 ]; then
    echo "error: the official install failed and PS2BUILD_SDK_NO_FALLBACK=1" >&2
    exit 1
else
    log "the official install failed; falling back to the mirror"
    [ -n "${GITHUB_ACTIONS:-}" ] && echo "::warning::$OFFICIAL failed; ps2build SDK installed from the ps2build-mirror release"
    from_mirror || { echo "error: both $OFFICIAL and the mirror failed" >&2; exit 1; }
    source=mirror
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
