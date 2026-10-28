#!/bin/sh
# Install the newest ps2build SDK into DEST, from git.techwritescode.dev or, when it is down, our mirror.
#
# Usage: ps2build_sdk.sh [--llvm] DEST
#        ps2build_sdk.sh --resolve [--llvm] OUT    (CI, before the cache step: write the versions to OUT)
#
# The pin file (PS2BUILD_SDK_PIN, default .github/ps2build-sdk.env) lists the components. A bare name
# follows the newest official release; name=vX holds that component at vX, to step around a bad upstream
# release. Each job resolves the versions from the official release API, then installs from:
#   1. DEST, when it already holds exactly those versions (an actions/cache restore).
#   2. The official server, "ps2build update <component> --version <tag>" for each component that differs,
#      so an older cached SDK only downloads what changed. The whole attempt gets
#      PS2BUILD_SDK_OFFICIAL_BUDGET seconds (default 900), each component one retry.
#   3. The newest NathanNeurotic/ps2build-mirror release, when the official install fails. Every file is
#      checked against that release's SHA256SUMS. When it is older than the official latest, the job says
#      so and sets PS2BUILD_SDK_STALE=true, so CI does not cache it under the latest versions.
# When the release API itself is unreachable, the newest mirror release defines the versions.
# PS2BUILD_SDK_NO_FALLBACK=1 fails instead of using the mirror. On GitHub Actions DEST is added to PATH
# and PS2BUILD_SDK_SOURCE (cache, official or mirror) to the environment.
set -eu

usage() {
    echo "usage: $0 [--llvm] DEST | $0 --resolve [--llvm] OUT" >&2
    exit 2
}

MODE=install
LLVM=0
while [ $# -gt 0 ]; do
    case $1 in
        --resolve) MODE=resolve ;;
        --official-only) MODE=official ;; # internal: the official attempt, run under one timeout
        --llvm) LLVM=1 ;;
        -*) usage ;;
        *) break ;;
    esac
    shift
done
[ $# -eq 1 ] || usage
DEST=$1
PIN=${PS2BUILD_SDK_PIN:-.github/ps2build-sdk.env}
OFFICIAL=${PS2BUILD_SDK_OFFICIAL_URL:-https://git.techwritescode.dev}
STALE=0

[ -f "$PIN" ] || { echo "error: pin file $PIN not found" >&2; exit 2; }
PS2BUILD_SDK_MIRROR= PS2BUILD_SDK_TAG= PS2BUILD_SDK_SUMS_SHA256= PS2BUILD_COMPONENTS= PS2BUILD_LLVM_TOOLCHAIN=
PS2BUILD_EXTRAS= PS2BUILD_SDK_RESOLVED=
# shellcheck disable=SC1090
case $PIN in /*) . "$PIN" ;; *) . "./$PIN" ;; esac
[ -n "$PS2BUILD_COMPONENTS" ] || { echo "error: $PIN sets no PS2BUILD_COMPONENTS" >&2; exit 2; }
PS2BUILD_SDK_MIRROR=${PS2BUILD_SDK_MIRROR:-NathanNeurotic/ps2build-mirror}
PS2BUILD_LLVM_TOOLCHAIN=${PS2BUILD_LLVM_TOOLCHAIN:-latest}
MIRROR_LATEST=${PS2BUILD_SDK_MIRROR_LATEST_URL:-https://github.com/$PS2BUILD_SDK_MIRROR/releases/latest/download}

log() { echo "ps2build-sdk: $*"; }

warn() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::warning::$*"; else log "warning: $*"; fi
}

differences() { # "name=version ..." of the mirror and of the latest -> "name mirror (latest latest), ..."
    for pair in $2; do
        mirror=$(version_in "${pair%%=*}" "$1" || echo none)
        [ "$mirror" = "${pair#*=}" ] || printf '%s %s (latest %s) ' "${pair%%=*}" "$mirror" "${pair#*=}"
    done
}

fetch() { # url file
    # --speed-limit: a stalled transfer is aborted (and retried) instead of hanging the job.
    curl -sSfL --retry 6 --retry-delay 5 --retry-all-errors --connect-timeout 20 --speed-limit 1024 --speed-time 60 "$1" -o "$2"
}

sha256() { sha256sum "$1" | cut -d' ' -f1; }

# Versions, tags and hashes from the network end up in a pin file that later steps source, so anything
# but these characters is refused (an empty result, as if the value were missing).
pin_value() { # name file -> the value a pin.env assigns, without quotes
    sed -n "s/^$1=\"\{0,1\}\([A-Za-z0-9._+= -]*\)\"\{0,1\}\$/\1/p" "$2" | head -n 1
}

version_in() { # component "name=version ..." -> version
    for pair in $2; do
        [ "${pair%%=*}" = "$1" ] && { echo "${pair#*=}"; return 0; }
    done
    return 1
}

version_of() { version_in "$1" "$PS2BUILD_COMPONENTS"; }

extra_var() { echo "$1" | tr 'a-z-' 'A-Z_'; } # ogg -> OGG, read as OGG_VERSION

latest_tag() { # repository -> its newest official release tag
    curl -sSfL --retry 3 --retry-delay 5 --retry-all-errors --connect-timeout 20 --max-time 60 \
        "$OFFICIAL/api/v1/repos/ps2/$1/releases/latest" | sed -n 's/.*"tag_name":"\([^"]*\)".*/\1/p' | head -n 1 | grep -E '^[A-Za-z0-9._+-]+$'
}

resolve_official() { # fill in each unpinned version with the newest official release
    resolved=
    for entry in $PS2BUILD_COMPONENTS; do
        case $entry in *=*) ;; *) entry="$entry=$(latest_tag "$entry")" || return 1 ;; esac
        resolved="$resolved${resolved:+ }$entry"
    done
    if [ "$PS2BUILD_LLVM_TOOLCHAIN" = latest ]; then
        llvm_version=$(latest_tag llvm-toolchain) || return 1
    else
        llvm_version=$PS2BUILD_LLVM_TOOLCHAIN
    fi
    extras=
    for name in $PS2BUILD_EXTRAS; do
        eval "version=\${$(extra_var "$name")_VERSION:-latest}"
        [ "$version" = latest ] && { version=$(latest_tag "$name") || return 1; }
        extras="$extras $(extra_var "$name")_VERSION=$version"
    done
    PS2BUILD_COMPONENTS=$resolved
    PS2BUILD_LLVM_TOOLCHAIN=$llvm_version
    for assignment in $extras; do eval "$assignment"; done
}

resolve_mirror() { # the release API is down: the newest mirror release defines the unpinned versions
    pin_tmp=$(mktemp)
    if ! fetch "$MIRROR_LATEST/pin.env" "$pin_tmp"; then
        rm -f "$pin_tmp"
        return 1
    fi
    mirror_components=$(pin_value PS2BUILD_COMPONENTS "$pin_tmp")
    resolved=
    for entry in $PS2BUILD_COMPONENTS; do
        case $entry in *=*) ;; *) entry="$entry=$(version_in "$entry" "$mirror_components")" || { rm -f "$pin_tmp"; return 1; } ;; esac
        resolved="$resolved${resolved:+ }$entry"
    done
    PS2BUILD_COMPONENTS=$resolved
    [ "$PS2BUILD_LLVM_TOOLCHAIN" = latest ] && PS2BUILD_LLVM_TOOLCHAIN=$(pin_value PS2BUILD_LLVM_TOOLCHAIN "$pin_tmp")
    for name in $PS2BUILD_EXTRAS; do
        var=$(extra_var "$name")_VERSION
        eval "version=\${$var:-latest}"
        [ "$version" = latest ] && eval "$var=\$(pin_value $var \"\$pin_tmp\")"
    done
    PS2BUILD_SDK_TAG=$(pin_value PS2BUILD_SDK_TAG "$pin_tmp")
    PS2BUILD_SDK_SUMS_SHA256=$(pin_value PS2BUILD_SDK_SUMS_SHA256 "$pin_tmp")
    rm -f "$pin_tmp"
    [ -n "$PS2BUILD_SDK_TAG" ] && [ -n "$PS2BUILD_SDK_SUMS_SHA256" ] || { log "$MIRROR_LATEST/pin.env is incomplete"; return 1; }
    warn "$OFFICIAL's release API is unreachable; using the newest ps2build-mirror release $PS2BUILD_SDK_TAG, which may be older than the latest SDK"
}

write_pin() { # the resolved versions, in the pin format later steps read
    {
        echo "# Resolved by ps2build_sdk.sh from $PIN"
        echo "PS2BUILD_SDK_RESOLVED=1"
        echo "PS2BUILD_SDK_MIRROR=$PS2BUILD_SDK_MIRROR"
        echo "PS2BUILD_SDK_TAG=$PS2BUILD_SDK_TAG"
        echo "PS2BUILD_SDK_SUMS_SHA256=$PS2BUILD_SDK_SUMS_SHA256"
        echo "PS2BUILD_COMPONENTS=\"$PS2BUILD_COMPONENTS\""
        echo "PS2BUILD_LLVM_TOOLCHAIN=$PS2BUILD_LLVM_TOOLCHAIN"
        echo "PS2BUILD_EXTRAS=\"$PS2BUILD_EXTRAS\""
        for name in $PS2BUILD_EXTRAS; do
            var=$(extra_var "$name")_VERSION
            eval "echo \"$var=\${$var}\""
        done
    } > "$1"
}

if [ "$PS2BUILD_SDK_RESOLVED" != 1 ]; then
    if resolve_official; then
        :
    elif [ "${PS2BUILD_SDK_NO_FALLBACK:-0}" = 1 ]; then
        echo "error: $OFFICIAL's release API is unreachable and PS2BUILD_SDK_NO_FALLBACK=1" >&2
        exit 1
    else
        resolve_mirror || { echo "error: neither $OFFICIAL nor the mirror's newest release is reachable" >&2; exit 1; }
    fi
fi
[ "$LLVM" = 0 ] || [ -n "$PS2BUILD_LLVM_TOOLCHAIN" ] || { echo "error: no llvm-toolchain version" >&2; exit 1; }

if [ "$MODE" = resolve ]; then
    write_pin "$DEST"
    # The key covers what lands in DEST: the components, llvm-toolchain for --llvm, and the extra packages.
    key=$(grep -v '^PS2BUILD_SDK_\(TAG\|SUMS_SHA256\)=' "$DEST" | { if [ "$LLVM" = 1 ]; then cat; else grep -v '^PS2BUILD_LLVM_TOOLCHAIN='; fi; } \
        | sha256sum | cut -c1-16)
    log "versions: $PS2BUILD_COMPONENTS$([ "$LLVM" = 1 ] && echo " llvm-toolchain=$PS2BUILD_LLVM_TOOLCHAIN")"
    if [ -n "${GITHUB_ENV:-}" ]; then
        echo "PS2BUILD_SDK_PIN=$DEST" >> "$GITHUB_ENV"
    fi
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        echo "key=$key" >> "$GITHUB_OUTPUT"
    fi
    echo "key=$key"
    exit 0
fi

wanted() { # "component version" lines this job needs
    for pair in $PS2BUILD_COMPONENTS; do
        echo "${pair%%=*} ${pair#*=}"
    done
    [ "$LLVM" = 1 ] && echo "llvm-toolchain $PS2BUILD_LLVM_TOOLCHAIN"
    return 0
}

has_version() { # component version: DEST's versions.yaml records it
    grep -Eq "^ +$1: $2\$" "$DEST/versions.yaml" 2>/dev/null
}

installed_ok() { # DEST holds every wanted component at its resolved version
    [ -x "$DEST/ps2build" ] && [ -f "$DEST/versions.yaml" ] || return 1
    wanted | while read -r name version; do
        if [ "$name" = ps2build ]; then
            "$DEST/ps2build" --version 2>/dev/null | grep -qx "ps2build $version" || { echo missing; break; }
        else
            has_version "$name" "$version" || { echo missing; break; }
        fi
    done | grep -q missing && return 1
    [ "$LLVM" = 0 ] || [ -f "$DEST/llvm-toolchain/ee/bin/mips64r5900el-ps2-elf-clang.cfg" ] || return 1
    return 0
}

reset_dest() { # never delete something that is not an SDK install
    if [ -e "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
        # ps2build may be a partial download from an interrupted official attempt, so not -x.
        if [ ! -f "$DEST/versions.yaml" ] && [ ! -f "$DEST/ps2build" ] && [ ! -f "$DEST/ps2build.new" ]; then
            echo "error: $DEST is not empty and is not a ps2build SDK; refusing to replace it" >&2
            exit 1
        fi
        log "replacing the SDK in $DEST"
        rm -rf "$DEST"
    fi
    mkdir -p "$DEST"
}

adopt_mirror_pin() { # pin.env of the newest mirror release: install that, and say so when it is older
    tag=$(pin_value PS2BUILD_SDK_TAG "$1")
    sums=$(pin_value PS2BUILD_SDK_SUMS_SHA256 "$1")
    components=$(pin_value PS2BUILD_COMPONENTS "$1")
    llvm=$(pin_value PS2BUILD_LLVM_TOOLCHAIN "$1")
    [ -n "$tag" ] && [ -n "$sums" ] && [ -n "$components" ] || { log "$MIRROR_LATEST/pin.env is incomplete"; return 1; }
    [ "$LLVM" = 1 ] && components_llvm="$components llvm-toolchain=$llvm" latest_llvm="$PS2BUILD_COMPONENTS llvm-toolchain=$PS2BUILD_LLVM_TOOLCHAIN" \
        || components_llvm=$components latest_llvm=$PS2BUILD_COMPONENTS
    older=$(differences "$components_llvm" "$latest_llvm")
    if [ -n "$older" ]; then
        STALE=1
        warn "the newest ps2build-mirror release $tag is older than the latest SDK: ${older% }"
    fi
    PS2BUILD_SDK_TAG=$tag PS2BUILD_SDK_SUMS_SHA256=$sums PS2BUILD_COMPONENTS=$components PS2BUILD_LLVM_TOOLCHAIN=$llvm
    for name in $PS2BUILD_EXTRAS; do
        var=$(extra_var "$name")_VERSION
        eval "$var=\$(pin_value $var \"\$1\")"
    done
    # Later steps (the ogg/vorbis packages) then take the same release.
    if [ "$PS2BUILD_SDK_RESOLVED" = 1 ]; then
        write_pin "$PIN"
    fi
}

from_mirror() {
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    if [ -z "$PS2BUILD_SDK_TAG" ]; then
        fetch "$MIRROR_LATEST/pin.env" "$work/pin.env" || { log "cannot download $MIRROR_LATEST/pin.env"; return 1; }
        adopt_mirror_pin "$work/pin.env" || return 1
    fi
    base=${PS2BUILD_SDK_MIRROR_URL:-https://github.com/$PS2BUILD_SDK_MIRROR/releases/download/$PS2BUILD_SDK_TAG}
    fetch "$base/SHA256SUMS" "$work/SHA256SUMS" || { log "cannot download $base/SHA256SUMS"; return 1; }
    got=$(sha256 "$work/SHA256SUMS")
    if [ "$got" != "$PS2BUILD_SDK_SUMS_SHA256" ]; then
        echo "error: mirror SHA256SUMS is $got, its pin.env says $PS2BUILD_SDK_SUMS_SHA256" >&2
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
    installed_ok || { echo "error: the mirror archives do not hold the versions their pin.env names" >&2; return 1; }
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
    # An older SDK (a cache restore of an earlier release) is updated in place: only what changed is fetched.
    if [ ! -f "$DEST/versions.yaml" ] || [ ! -x "$DEST/ps2build" ]; then
        reset_dest
    fi
    version=$(version_of ps2build)
    if ! "$DEST/ps2build" --version 2>/dev/null | grep -qx "ps2build $version"; then
        log "downloading ps2build $version from $OFFICIAL"
        fetch "$OFFICIAL/ps2/ps2build/releases/download/$version/ps2build-$version-linux-amd64" "$DEST/ps2build.new" || return 1
        chmod +x "$DEST/ps2build.new"
        mv -f "$DEST/ps2build.new" "$DEST/ps2build"
    fi
    wanted | while read -r name version; do
        [ "$name" = ps2build ] && continue
        has_version "$name" "$version" && continue
        update_component "$name" "$version" || exit 1
    done || return 1
    installed_ok || { echo "error: the official install does not hold the resolved versions" >&2; return 1; }
}

if [ "$MODE" = official ]; then
    echo $$ > "$PS2BUILD_SDK_PGID_FILE"
    from_official
    exit
fi

official() { # the official attempt in its own process group, so a timeout also stops ps2build and curl
    PS2BUILD_SDK_PGID_FILE=$(mktemp)
    export PS2BUILD_SDK_PGID_FILE
    resolved_pin=$(mktemp)
    write_pin "$resolved_pin"
    llvm_flag=
    [ "$LLVM" = 1 ] && llvm_flag=--llvm
    status=0
    PS2BUILD_SDK_PIN=$resolved_pin timeout "${PS2BUILD_SDK_OFFICIAL_BUDGET:-900}" \
        setsid sh "$0" --official-only $llvm_flag "$DEST" || status=$?
    if [ "$status" != 0 ]; then
        [ "$status" = 124 ] && log "the official install took longer than ${PS2BUILD_SDK_OFFICIAL_BUDGET:-900} s"
        pgid=$(cat "$PS2BUILD_SDK_PGID_FILE" 2>/dev/null || true)
        # Nothing left over may keep writing into DEST while the mirror replaces it.
        [ -n "$pgid" ] && env kill -KILL -- "-$pgid" 2>/dev/null || true  # dash's kill takes no process group
    fi
    rm -f "$PS2BUILD_SDK_PGID_FILE" "$resolved_pin"
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
    warn "$OFFICIAL failed; installing the ps2build SDK from the ps2build-mirror release"
    from_mirror || { echo "error: both $OFFICIAL and the mirror failed" >&2; exit 1; }
    source=mirror
fi
rm -rf "$DEST/.update-staging" "$DEST/ps2build.new"

log "SDK ready in $DEST (from $source)"
if [ -n "${GITHUB_ENV:-}" ]; then
    echo "$DEST" >> "$GITHUB_PATH"
    echo "PS2BUILD_SDK_SOURCE=$source" >> "$GITHUB_ENV"
    [ "$STALE" = 1 ] && echo "PS2BUILD_SDK_STALE=true" >> "$GITHUB_ENV"
fi
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    echo "ps2build SDK from **$source**: \`$PS2BUILD_COMPONENTS\`$([ "$STALE" = 1 ] && echo " (older than the latest official release)")" >> "$GITHUB_STEP_SUMMARY"
fi
